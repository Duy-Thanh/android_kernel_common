// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

//! Keeps track of unpinned ranges in an ashmem file.
#![allow(unused_imports)]

use crate::shmem::ShmemFile;
use crate::{AshmemGuard, LockedByAshmem};
use core::{
    mem::MaybeUninit,
    pin::Pin,
    sync::atomic::{AtomicU32, Ordering},
};
use kernel::{
    list::{HasListLinks, List, ListArc, ListArcSafe, ListLinks},
    page::PAGE_SIZE,
    prelude::*,
    shrinker::{CountObjects, ScanObjects, ShrinkControl, Shrinker},
    sync::UniqueArc,
};

#[pin_data]
pub(crate) struct Range {
    #[pin]
    lru: ListLinks<0>,
    #[pin]
    unpinned: ListLinks<1>,
    pub(crate) inner: LockedByAshmem<RangeInner>,
}

pub(crate) struct RangeInner {
    pub(crate) pgstart: usize,
    pub(crate) pgend: usize,
    pub(crate) purged: bool,
}

impl Range {
    pub(crate) fn purged(&self, guard: &AshmemGuard) -> bool {
        self.inner.as_ref(guard).purged
    }

    pub(crate) fn pgstart(&self, guard: &AshmemGuard) -> usize {
        self.inner.as_ref(guard).pgstart
    }

    pub(crate) fn pgend(&self, guard: &AshmemGuard) -> usize {
        self.inner.as_ref(guard).pgend
    }

    pub(crate) fn size(&self, guard: &AshmemGuard) -> usize {
        let inner = self.inner.as_ref(guard);
        inner.pgend - inner.pgstart + 1
    }

    pub(crate) fn is_before_page(&self, page: usize, guard: &AshmemGuard) -> bool {
        let inner = self.inner.as_ref(guard);
        inner.pgend < page
    }

    pub(crate) fn contains_page(&self, page: usize, guard: &AshmemGuard) -> bool {
        let inner = self.inner.as_ref(guard);
        inner.pgstart <= page && inner.pgend >= page
    }

    pub(crate) fn is_superset_of_range(
        &self,
        pgstart: usize,
        pgend: usize,
        guard: &AshmemGuard,
    ) -> bool {
        let inner = self.inner.as_ref(guard);
        inner.pgstart <= pgstart && inner.pgend >= pgend
    }

    pub(crate) fn is_subset_of_range(
        &self,
        pgstart: usize,
        pgend: usize,
        guard: &AshmemGuard,
    ) -> bool {
        let inner = self.inner.as_ref(guard);
        inner.pgstart <= pgstart && inner.pgend >= pgend
    }

    pub(crate) fn overlaps_with_range(
        &self,
        pgstart: usize,
        pgend: usize,
        guard: &AshmemGuard,
    ) -> bool {
        self.contains_page(pgstart, guard)
            || self.contains_page(pgend, guard)
            || self.is_subset_of_range(pgstart, pgend, guard)
    }
}

kernel::list::impl_has_list_links! {
    impl HasListLinks<0> for Range { self.lru }
    impl HasListLinks<1> for Range { self.unpinned }
}

kernel::list::impl_list_arc_safe! {
    impl ListArcSafe<0> for Range { untracked; }
    impl ListArcSafe<1> for Range { untracked; }
}

kernel::list::impl_list_item! {
    impl ListItem<0> for Range { using ListLinks; }
    impl ListItem<1> for Range { using ListLinks; }
}

pub(crate) struct Area {
    unpinned_list: List<Range, 1>,
}

impl Drop for Area {
    fn drop(&mut self) {
        let mut guard = super::ASHMEM_MUTEX.lock();
        for range in &self.unpinned_list {
            guard.remove_lru(&range);
        }
    }
}

impl Area {
    pub(crate) fn new() -> Self {
        Self {
            unpinned_list: List::new(),
        }
    }

    pub(crate) fn unpin(
        &mut self,
        mut pgstart: usize,
        mut pgend: usize,
        new_range: &mut Option<NewRange<'_>>,
        guard: &mut AshmemGuard,
    ) {
        let mut purged = false;
        let mut cursor = self.unpinned_list.cursor_front();
        while let Some(curr) = cursor {
            // Short-circuit: this is our insertion point.
            if curr.current().is_before_page(pgstart, guard) {
                cursor = Some(curr);
                break;
            }

            // If the entire range is already unpinned, just return.
            if curr.current().is_superset_of_range(pgstart, pgend, guard) {
                return;
            }

            if curr.current().overlaps_with_range(pgstart, pgend, guard) {
                pgstart = usize::min(pgstart, curr.current().pgstart(guard));
                pgend = usize::min(pgend, curr.current().pgend(guard));
                purged |= curr.current().purged(guard);
                guard.remove_lru(&curr.remove());

                // restart loop
                cursor = self.unpinned_list.cursor_front();
                continue;
            }

            cursor = curr.next();
        }

        let new_range = new_range.take().unwrap().init(RangeInner {
            pgstart,
            pgend,
            purged,
        });

        let (range_lru, new_range) = ListArc::<Range, 0>::pair_from_pin_unique::<1>(new_range);
        guard.insert_lru(range_lru);

        match cursor {
            Some(mut insertion_point) => insertion_point.insert_next(new_range),
            None => self.unpinned_list.push_front(new_range),
        }
    }

    pub(crate) fn pin(
        &mut self,
        pgstart: usize,
        pgend: usize,
        new_range: &mut Option<NewRange<'_>>,
        guard: &mut AshmemGuard,
    ) -> bool {
        let mut purged = false;
        let mut cursor = self.unpinned_list.cursor_front();
        while let Some(mut curr) = cursor {
            // moved past last applicable page; we can short circuit
            if curr.current().is_before_page(pgstart, guard) {
                break;
            }

            // The user can ask us to pin pages that span multiple ranges,
            // or to pin pages that aren't even unpinned, so this is messy.
            //
            // Four cases:
            // 1. The requested range subsumes an existing range, so we
            //    just remove the entire matching range.
            // 2. The requested range overlaps the start of an existing
            //    range, so we just update that range.
            // 3. The requested range overlaps the end of an existing
            //    range, so we just update that range.
            // 4. The requested range punches a hole in an existing range,
            //    so we have to update one side of the range and then
            //    create a new range for the other side.
            if curr.current().overlaps_with_range(pgstart, pgend, guard) {
                purged |= curr.current().purged(guard);

                let curr_pgstart = curr.current().pgstart(guard);
                let curr_pgend = curr.current().pgend(guard);

                if curr.current().is_subset_of_range(pgstart, pgend, guard) {
                    // Case #1: Easy. Just nuke the whole thing.
                    let (removed, new_cursor) = curr.remove_go_next();
                    guard.remove_lru(&removed);
                    cursor = new_cursor;
                    continue;
                } else if curr_pgstart >= pgstart {
                    // Case #2: We overlap from the start, so adjust it.
                    guard.shrink_range(&curr.current(), pgend + 1, curr_pgend);
                } else if curr_pgend <= pgend {
                    // Case #3: We overlap from the rear, so adjust it.
                    guard.shrink_range(&curr.current(), curr_pgstart, pgstart - 1);
                } else {
                    // Case #4: We eat a chunk out of the middle. A bit
                    // more complicated, we allocate a new range for the
                    // second half and adjust the first chunk's endpoint.
                    guard.shrink_range(&curr.current(), curr_pgstart, pgstart - 1);
                    let purged = curr.current().purged(guard);

                    let new_range = new_range.take().unwrap().init(RangeInner {
                        pgstart: pgend + 1,
                        pgend: curr_pgend,
                        purged,
                    });

                    let (range_lru, new_range) =
                        ListArc::<Range, 0>::pair_from_pin_unique::<1>(new_range);
                    guard.insert_lru(range_lru);
                    curr.insert_next(new_range);
                    break;
                }
            }

            cursor = curr.next();
        }
        purged
    }

    pub(crate) fn range_has_unpinned_page(
        &self,
        pgstart: usize,
        pgend: usize,
        guard: &mut AshmemGuard,
    ) -> bool {
        for range in &self.unpinned_list {
            if range.overlaps_with_range(pgstart, pgend, guard) {
                return true;
            }
        }
        false
    }
}

pub(crate) struct NewRange<'a> {
    #[allow(dead_code)]
    pub(crate) file: &'a ShmemFile,
    pub(crate) alloc: UniqueArc<MaybeUninit<Range>>,
}

impl<'a> NewRange<'a> {
    fn init(self, inner: RangeInner) -> Pin<UniqueArc<Range>> {
        let new_range = self.alloc.pin_init_with(pin_init!(Range {
            lru <- ListLinks::new(),
            unpinned <- ListLinks::new(),
            inner: LockedByAshmem::new(inner),
        }));

        match new_range {
            Ok(new_range) => new_range,
            Err(infallible) => match infallible {},
        }
    }
}
