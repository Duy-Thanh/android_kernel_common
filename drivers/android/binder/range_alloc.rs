// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2024 Google LLC.

use kernel::{
    macros::kunit_tests,
    page::PAGE_SIZE,
    prelude::*,
    rbtree::{RBTree, RBTreeNode, RBTreeNodeReservation},
    seq_file::SeqFile,
    seq_print,
    task::Pid,
};

/// Keeps track of allocations in a process' mmap.
///
/// Each process has an mmap where the data for incoming transactions will be placed. This struct
/// keeps track of allocations made in the mmap. For each allocation, we store a descriptor that
/// has metadata related to the allocation. We also keep track of available free space.
pub(crate) struct RangeAllocator<T> {
    tree: RBTree<usize, Descriptor<T>>,
    free_tree: RBTree<FreeKey, ()>,
    size: usize,
    free_oneway_space: usize,
    pub(crate) oneway_spam_detected: bool,
}

/// Represents a range of pages that have just become completely free.
#[derive(Copy, Clone)]
pub(crate) struct FreedRange {
    pub(crate) start_page_idx: usize,
    pub(crate) end_page_idx: usize,
}

impl FreedRange {
    fn interior_pages(offset: usize, size: usize) -> FreedRange {
        FreedRange {
            // Divide round up
            start_page_idx: (offset + (PAGE_SIZE - 1)) / PAGE_SIZE,
            // Divide round down
            end_page_idx: (offset + size) / PAGE_SIZE,
        }
    }
}

impl<T> RangeAllocator<T> {
    pub(crate) fn new(size: usize) -> Result<Self> {
        let mut tree = RBTree::new();
        tree.try_create_and_insert(0, Descriptor::new(0, size))?;
        let mut free_tree = RBTree::new();
        free_tree.try_create_and_insert((size, 0), ())?;
        Ok(Self {
            free_oneway_space: size / 2,
            tree,
            free_tree,
            oneway_spam_detected: false,
            size,
        })
    }

    pub(crate) fn debug_print(&self, m: &mut SeqFile) -> Result<()> {
        for desc in self.tree.values() {
            let state = match &desc.state {
                Some(state) => state,
                None => continue,
            };
            seq_print!(
                m,
                "  buffer {}: {} size {} pid {} oneway {}",
                0,
                desc.offset,
                desc.size,
                state.pid(),
                state.is_oneway()
            );
            match state {
                DescriptorState::Reserved(_res) => {
                    seq_print!(m, "reserved\n");
                }
                DescriptorState::Allocated(_alloc) => {
                    seq_print!(m, "allocated\n");
                }
            }
        }
        Ok(())
    }

    fn find_best_match(&mut self, size: usize) -> Option<&mut Descriptor<T>> {
        let free_cursor = self.free_tree.cursor_lower_bound(&(size, 0))?;
        let ((_, offset), _) = free_cursor.current();
        self.tree.get_mut(offset)
    }

    /// Try to reserve a new buffer, using the provided allocation if necessary.
    pub(crate) fn reserve_new(
        &mut self,
        size: usize,
        is_oneway: bool,
        pid: Pid,
        alloc: ReserveNewBox<T>,
    ) -> Result<usize> {
        // Compute new value of free_oneway_space, which is set only on success.
        let new_oneway_space = if is_oneway {
            match self.free_oneway_space.checked_sub(size) {
                Some(new_oneway_space) => new_oneway_space,
                None => return Err(ENOSPC),
            }
        } else {
            self.free_oneway_space
        };

        // Start detecting spammers once we have less than 20%
        // of async space left (which is less than 10% of total
        // buffer size).
        //
        // (This will short-circut, so `low_oneway_space` is
        // only called when necessary.)
        self.oneway_spam_detected =
            is_oneway && new_oneway_space < self.size / 10 && self.low_oneway_space(pid);

        let (found_size, found_off, tree_node, free_tree_node) = match self.find_best_match(size) {
            None => {
                pr_warn!("ENOSPC from range_alloc.reserve_new - size: {}", size);
                return Err(ENOSPC);
            }
            Some(desc) => {
                let found_size = desc.size;
                let found_offset = desc.offset;

                // In case we need to break up the descriptor
                let new_desc = Descriptor::new(found_offset + size, found_size - size);
                let (tree_node, free_tree_node, desc_node_res) = alloc.initialize(new_desc);

                desc.state = Some(DescriptorState::new(is_oneway, pid, desc_node_res));
                desc.size = size;

                (found_size, found_offset, tree_node, free_tree_node)
            }
        };
        self.free_oneway_space = new_oneway_space;
        self.free_tree.remove(&(found_size, found_off));

        if found_size != size {
            self.tree.insert(tree_node);
            self.free_tree.insert(free_tree_node);
        }

        Ok(found_off)
    }

    pub(crate) fn reservation_abort(&mut self, offset: usize) -> Result<FreedRange> {
        let mut cursor = self.tree.cursor_lower_bound(&offset).ok_or_else(|| {
            pr_warn!(
                "EINVAL from range_alloc.reservation_abort - offset: {}",
                offset
            );
            EINVAL
        })?;

        let (_, desc) = cursor.current_mut();

        if desc.offset != offset {
            pr_warn!(
                "EINVAL from range_alloc.reservation_abort - offset: {}",
                offset
            );
            return Err(EINVAL);
        }

        let reservation = desc.try_change_state(|state| match state {
            Some(DescriptorState::Reserved(reservation)) => (None, Ok(reservation)),
            None => {
                pr_warn!(
                    "EINVAL from range_alloc.reservation_abort - offset: {}",
                    offset
                );
                (None, Err(EINVAL))
            }
            allocated => {
                pr_warn!(
                    "EPERM from range_alloc.reservation_abort - offset: {}",
                    offset
                );
                (allocated, Err(EPERM))
            }
        })?;

        let mut size = desc.size;
        let mut offset = desc.offset;
        let free_oneway_space_add = if reservation.is_oneway { size } else { 0 };

        self.free_oneway_space += free_oneway_space_add;

        let mut freed_range = FreedRange::interior_pages(offset, size);
        // Compute how large the next free region needs to be to include one more page in
        // the newly freed range.
        let add_next_page_needed = match (offset + size) % PAGE_SIZE {
            0 => usize::MAX,
            unalign => PAGE_SIZE - unalign,
        };
        // Compute how large the previous free region needs to be to include one more page
        // in the newly freed range.
        let add_prev_page_needed = match offset % PAGE_SIZE {
            0 => usize::MAX,
            unalign => unalign,
        };

        // Merge next into current if next is free
        let remove_next = match cursor.peek_next() {
            Some((_, next)) if next.state.is_none() => {
                if next.size >= add_next_page_needed {
                    freed_range.end_page_idx += 1;
                }
                self.free_tree.remove(&(next.size, next.offset));
                size += next.size;
                true
            }
            _ => false,
        };

        if remove_next {
            let (_, desc) = cursor.current_mut();
            desc.size = size;
            cursor.remove_next();
        }

        // Merge current into prev if prev is free
        match cursor.peek_prev_mut() {
            Some((_, prev)) if prev.state.is_none() => {
                if prev.size >= add_prev_page_needed {
                    freed_range.start_page_idx -= 1;
                }
                // merge previous with current, remove current
                self.free_tree.remove(&(prev.size, prev.offset));
                offset = prev.offset;
                size += prev.size;
                prev.size = size;
                cursor.remove_current();
            }
            _ => {}
        };

        self.free_tree
            .insert(reservation.free_res.into_node((size, offset), ()));

        Ok(freed_range)
    }

    pub(crate) fn reservation_commit(&mut self, offset: usize, data: Option<T>) -> Result {
        let desc = self.tree.get_mut(&offset).ok_or_else(|| {
            pr_warn!(
                "ENOENT from range_alloc.reservation_commit - offset: {}",
                offset
            );
            ENOENT
        })?;

        desc.try_change_state(|state| match state {
            Some(DescriptorState::Reserved(reservation)) => (
                Some(DescriptorState::Allocated(reservation.allocate(data))),
                Ok(()),
            ),
            other => {
                pr_warn!(
                    "ENOENT from range_alloc.reservation_commit - offset: {}",
                    offset
                );
                (other, Err(ENOENT))
            }
        })
    }

    /// Takes an entry at the given offset from [`DescriptorState::Allocated`] to
    /// [`DescriptorState::Reserved`].
    ///
    /// Returns the size of the existing entry and the data associated with it.
    pub(crate) fn reserve_existing(&mut self, offset: usize) -> Result<(usize, Option<T>)> {
        let desc = self.tree.get_mut(&offset).ok_or_else(|| {
            pr_warn!(
                "ENOENT from range_alloc.reserve_existing - offset: {}",
                offset
            );
            ENOENT
        })?;

        let data = desc.try_change_state(|state| match state {
            Some(DescriptorState::Allocated(allocation)) => {
                let (reservation, data) = allocation.deallocate();
                (Some(DescriptorState::Reserved(reservation)), Ok(data))
            }
            other => {
                pr_warn!(
                    "ENOENT from range_alloc.reserve_existing - offset: {}",
                    offset
                );
                (other, Err(ENOENT))
            }
        })?;

        Ok((desc.size, data))
    }

    /// Call the provided callback at every allocated region.
    ///
    /// This destroys the range allocator. Used only during shutdown.
    pub(crate) fn take_for_each<F: Fn(usize, usize, Option<T>)>(&mut self, callback: F) {
        for (_, desc) in self.tree.iter_mut() {
            if let Some(DescriptorState::Allocated(allocation)) = &mut desc.state {
                callback(desc.offset, desc.size, allocation.take());
            }
        }
    }

    /// Find the amount and size of buffers allocated by the current caller.
    ///
    /// The idea is that once we cross the threshold, whoever is responsible
    /// for the low async space is likely to try to send another async transaction,
    /// and at some point we'll catch them in the act.  This is more efficient
    /// than keeping a map per pid.
    fn low_oneway_space(&self, calling_pid: Pid) -> bool {
        let mut total_alloc_size = 0;
        let mut num_buffers = 0;
        for (_, desc) in self.tree.iter() {
            if let Some(state) = &desc.state {
                if state.is_oneway() && state.pid() == calling_pid {
                    total_alloc_size += desc.size;
                    num_buffers += 1;
                }
            }
        }

        // Warn if this pid has more than 50 transactions, or more than 50% of
        // async space (which is 25% of total buffer size). Oneway spam is only
        // detected when the threshold is exceeded.
        num_buffers > 50 || total_alloc_size > self.size / 4
    }
}

struct Descriptor<T> {
    size: usize,
    offset: usize,
    state: Option<DescriptorState<T>>,
}

impl<T> Descriptor<T> {
    fn new(offset: usize, size: usize) -> Self {
        Self {
            size,
            offset,
            state: None,
        }
    }

    fn try_change_state<F, Data>(&mut self, f: F) -> Result<Data>
    where
        F: FnOnce(Option<DescriptorState<T>>) -> (Option<DescriptorState<T>>, Result<Data>),
    {
        let (new_state, result) = f(self.state.take());
        self.state = new_state;
        result
    }
}

enum DescriptorState<T> {
    Reserved(Reservation),
    Allocated(Allocation<T>),
}

impl<T> DescriptorState<T> {
    fn new(is_oneway: bool, pid: Pid, free_res: FreeNodeRes) -> Self {
        DescriptorState::Reserved(Reservation {
            is_oneway,
            pid,
            free_res,
        })
    }

    fn pid(&self) -> Pid {
        match self {
            DescriptorState::Reserved(inner) => inner.pid,
            DescriptorState::Allocated(inner) => inner.pid,
        }
    }

    fn is_oneway(&self) -> bool {
        match self {
            DescriptorState::Reserved(inner) => inner.is_oneway,
            DescriptorState::Allocated(inner) => inner.is_oneway,
        }
    }
}

struct Reservation {
    is_oneway: bool,
    pid: Pid,
    free_res: FreeNodeRes,
}

impl Reservation {
    fn allocate<T>(self, data: Option<T>) -> Allocation<T> {
        Allocation {
            data,
            is_oneway: self.is_oneway,
            pid: self.pid,
            free_res: self.free_res,
        }
    }
}

struct Allocation<T> {
    is_oneway: bool,
    pid: Pid,
    free_res: FreeNodeRes,
    data: Option<T>,
}

impl<T> Allocation<T> {
    fn deallocate(self) -> (Reservation, Option<T>) {
        (
            Reservation {
                is_oneway: self.is_oneway,
                pid: self.pid,
                free_res: self.free_res,
            },
            self.data,
        )
    }

    fn take(&mut self) -> Option<T> {
        self.data.take()
    }
}

// (Descriptor.size, Descriptor.offset)
type FreeKey = (usize, usize);
type FreeNodeRes = RBTreeNodeReservation<FreeKey, ()>;

/// An allocation for use by `reserve_new`.
pub(crate) struct ReserveNewBox<T> {
    tree_node_res: RBTreeNodeReservation<usize, Descriptor<T>>,
    free_tree_node_res: FreeNodeRes,
    desc_node_res: FreeNodeRes,
}

impl<T> ReserveNewBox<T> {
    pub(crate) fn try_new() -> Result<Self> {
        let tree_node_res = RBTree::try_reserve_node()?;
        let free_tree_node_res = RBTree::try_reserve_node()?;
        let desc_node_res = RBTree::try_reserve_node()?;
        Ok(Self {
            tree_node_res,
            free_tree_node_res,
            desc_node_res,
        })
    }

    fn initialize(
        self,
        desc: Descriptor<T>,
    ) -> (
        RBTreeNode<usize, Descriptor<T>>,
        RBTreeNode<FreeKey, ()>,
        FreeNodeRes,
    ) {
        let size = desc.size;
        let offset = desc.offset;
        (
            self.tree_node_res.into_node(offset, desc),
            self.free_tree_node_res.into_node((size, offset), ()),
            self.desc_node_res,
        )
    }
}

#[kunit_tests(android_binder_ipc_rust_range_alloc)]
mod tests {
    use core::iter::Iterator;

    use crate::range_alloc::Allocation;
    use crate::range_alloc::Descriptor;
    use crate::range_alloc::DescriptorState;
    use crate::range_alloc::RangeAllocator;
    use crate::range_alloc::Reservation;
    use crate::range_alloc::ReserveNewBox;
    use kernel::prelude::*;
    use kernel::rbtree::{RBTree, RBTreeCursor};
    #[test]
    fn test_reserve_new() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let expected: &[Option<DescriptorState<usize>>] = &[None];
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        let offset = ra
            .reserve_new(4, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let expected = &[test_reserved(), None];
        assert_eq!(offset, 0);
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        let offset = ra
            .reserve_new(5, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let expected = &[test_reserved(), test_reserved(), None];
        assert_eq!(offset, 4);
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        let offset = ra
            .reserve_new(1, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let expected = &[test_reserved(), test_reserved(), test_reserved()];
        assert_eq!(offset, 9);
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        let offset = ra.reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap());
        assert!(offset.is_err());
    }

    #[test]
    fn test_reservation_abort_no_merge() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let expected: &[Option<DescriptorState<usize>>] =
            &[test_reserved(), test_reserved(), test_reserved(), None];
        ra.reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        ra.reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(offset_middle).unwrap();
        let expected = &[test_reserved(), None, test_reserved(), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);
    }

    #[test]
    fn test_reservation_abort_merge_right() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let expected: &[Option<DescriptorState<usize>>] =
            &[test_reserved(), test_reserved(), test_reserved(), None];
        ra.reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        ra.reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_right = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(offset_right).unwrap();
        let expected = &[test_reserved(), test_reserved(), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);
    }

    #[test]
    fn test_reservation_abort_merge_left() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let expected: &[Option<DescriptorState<usize>>] =
            &[test_reserved(), test_reserved(), test_reserved(), None];
        let offset_left = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        ra.reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(offset_left).unwrap();
        let expected = &[None, test_reserved(), test_reserved(), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(offset_middle).unwrap();
        let expected = &[None, test_reserved(), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);
    }

    #[test]
    fn test_reservation_abort_merge_both() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let expected: &[Option<DescriptorState<usize>>] =
            &[test_reserved(), test_reserved(), test_reserved(), None];
        let offset_left = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_right = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(offset_left).unwrap();
        ra.reservation_abort(offset_right).unwrap();
        let expected = &[None, test_reserved(), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(offset_middle).unwrap();
        let expected = &[None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);
    }

    #[test]
    fn test_reservation_commit() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let expected: &[Option<DescriptorState<usize>>] =
            &[test_reserved(), test_reserved(), test_reserved(), None];
        let offset_left = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_right = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(offset_left).unwrap();
        ra.reservation_abort(offset_right).unwrap();
        let expected = &[None, test_reserved(), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_commit(offset_middle, Some(1)).unwrap();
        let expected = &[None, test_allocated(Some(1)), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);
    }

    #[test]
    fn test_reserve_existing() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let expected: &[Option<DescriptorState<usize>>] =
            &[test_reserved(), test_reserved(), test_reserved(), None];
        let offset_left = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_right = ra
            .reserve_new(2, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(offset_left).unwrap();
        ra.reservation_abort(offset_right).unwrap();
        let expected = &[None, test_reserved(), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_commit(offset_middle, Some(1)).unwrap();
        let expected = &[None, test_allocated(Some(1)), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        let existing = ra.reserve_existing(offset_middle).unwrap();
        assert_eq!(existing, (2, Some(1)));
        let expected = &[None, test_reserved(), None];

        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);
    }

    #[test]
    fn test_end_to_end() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(1040384).unwrap();
        let expected: &[Option<DescriptorState<usize>>] = &[None];
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        let offset = ra
            .reserve_new(16, false, 1, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let expected = &[test_reserved(), None];
        assert_eq!(offset, 0);
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_commit(0, Some(1)).unwrap();
        let expected = &[test_allocated(Some(1)), None];
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        let existing = ra.reserve_existing(offset).unwrap();
        assert_eq!(existing, (16, Some(1)));

        let expected = &[test_reserved(), None];
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);

        ra.reservation_abort(0).unwrap();
        let expected = &[None];
        assert_invariant_and_state(ra.tree.cursor_front().unwrap(), &ra.free_tree, expected);
    }

    fn assert_invariant_and_state(
        cursor: RBTreeCursor<'_, usize, Descriptor<usize>>,
        free_tree: &RBTree<(usize, usize), ()>,
        expected: &[Option<DescriptorState<usize>>],
    ) {
        let mut index = 1;
        let (key, desc) = cursor.current();

        assert_eq!(key, &desc.offset);
        assert_desc_state_eq(&expected[0], &desc.state);

        // free descriptors should always have corresponding entries in the free tree
        if desc.state.is_none() {
            assert!(free_tree.get(&(desc.size, desc.offset)).is_some());
        }

        let mut last = (desc.offset, desc.size, copy_desc_state(&desc.state));
        let mut next = cursor.move_next();

        while let Some(n) = next {
            let (key, desc) = n.current();
            let (last_offset, last_size, last_state) = last;

            assert_eq!(key, &desc.offset);
            assert_desc_state_eq(&expected[index], &desc.state);

            // adjacent free descriptors should always be merged together
            assert!(match (&last_state, &desc.state) {
                (None, None) => false,
                _ => true,
            });

            // any descriptor's offset should always be a function of it's predecessors offset + size
            assert_eq!(desc.offset, last_offset + last_size);

            // free descriptors should always have corresponding entries in the free tree
            if desc.state.is_none() {
                assert!(free_tree.get(&(desc.size, desc.offset)).is_some());
            }

            last = (desc.offset, desc.size, copy_desc_state(&desc.state));
            index += 1;
            next = n.move_next();
        }

        assert!(expected.len() == index);

        // the free tree should not have extra entries
        let mut expected_free_count = 0;
        expected.iter().filter(|&e| e.is_none()).for_each(|_| {
            expected_free_count += 1;
        });

        let mut actual_free_count = 0;
        free_tree.iter().for_each(|_| {
            actual_free_count += 1;
        });

        assert_eq!(expected_free_count, actual_free_count);
    }

    fn assert_desc_state_eq(
        left: &Option<DescriptorState<usize>>,
        right: &Option<DescriptorState<usize>>,
    ) {
        match (left, right) {
            (None, None) => (),
            (Some(DescriptorState::Reserved(_)), Some(DescriptorState::Reserved(_))) => (),
            (Some(DescriptorState::Allocated(_)), Some(DescriptorState::Allocated(_))) => (),
            _ => panic!("descriptor states are not equal!"),
        }
    }

    fn copy_desc_state(state: &Option<DescriptorState<usize>>) -> Option<DescriptorState<usize>> {
        let Some(state) = state else {
            return None;
        };

        match state {
            DescriptorState::Reserved(Reservation { is_oneway, pid, .. }) => Some(
                DescriptorState::new(*is_oneway, *pid, RBTree::try_reserve_node().unwrap()),
            ),
            DescriptorState::Allocated(Allocation {
                is_oneway,
                pid,
                data,
                ..
            }) => Some(DescriptorState::Allocated(Allocation {
                is_oneway: *is_oneway,
                pid: *pid,
                free_res: RBTree::try_reserve_node().unwrap(),
                data: *data,
            })),
        }
    }

    fn test_reserved() -> Option<DescriptorState<usize>> {
        Some(DescriptorState::new(
            true,
            1,
            RBTree::try_reserve_node().unwrap(),
        ))
    }

    fn test_allocated(data: Option<usize>) -> Option<DescriptorState<usize>> {
        let allocation = Allocation {
            is_oneway: false,
            pid: 1,
            free_res: RBTree::try_reserve_node().unwrap(),
            data,
        };
        Some(DescriptorState::Allocated(allocation))
    }
}
