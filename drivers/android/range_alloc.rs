// SPDX-License-Identifier: GPL-2.0

use kernel::{
    // TODO: uncomment once test support is available
    // macros::kunit_tests,
    prelude::*,
    rbtree::{RBTree, RBTreeNode, RBTreeNodeReservation},
};

pub(crate) struct RangeAllocator<T> {
    tree: RBTree<usize, Descriptor<T>>,
    free_tree: RBTree<FreeKey, ()>,
    free_oneway_space: usize,
}

impl<T> RangeAllocator<T> {
    pub(crate) fn new(size: usize) -> Result<Self> {
        let mut tree = RBTree::new();
        tree.try_insert(0, Descriptor::new(0, size))?;
        let mut free_tree = RBTree::new();
        free_tree.try_insert((size, 0), ())?;
        Ok(Self {
            free_oneway_space: size / 2,
            tree,
            free_tree,
        })
    }

    fn find_best_match(&mut self, size: usize) -> Option<&mut Descriptor<T>> {
        let free_cursor = self.free_tree.cursor_lower_bound(&(size, 0))?;
        let ((_, offset), _) = free_cursor.current();
        self.tree.get_mut(&offset)
    }

    /// Try to reserve a new buffer, using the provided allocation if necessary.
    pub(crate) fn reserve_new(
        &mut self,
        size: usize,
        is_oneway: bool,
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

        let (found_size, found_offset, tree_node, free_tree_node) = match self.find_best_match(size)
        {
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

                desc.state = Some(DescriptorState::new(is_oneway, desc_node_res));
                desc.size = size;

                (found_size, found_offset, tree_node, free_tree_node)
            }
        };

        self.free_oneway_space = new_oneway_space;
        self.free_tree.remove(&(found_size, found_offset));

        if found_size != size {
            self.tree.insert(tree_node);
            self.free_tree.insert(free_tree_node);
        }

        Ok(found_offset)
    }

    pub(crate) fn reservation_abort(&mut self, offset: usize) -> Result {
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

        // Merge next into current if next is free
        let remove_next = match cursor.peek_next() {
            Some((_, next)) if next.state.is_none() => {
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

        Ok(())
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

    pub(crate) fn for_each<F: Fn(usize, usize, Option<T>)>(&mut self, callback: F) {
        let mut iter = self.tree.iter_mut();
        while let Some((_, desc)) = iter.next() {
            if let Some(DescriptorState::Allocated(allocation)) = &mut desc.state {
                callback(desc.offset, desc.size, allocation.take());
            }
        }
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
    fn new(is_oneway: bool, free_res: FreeNodeRes) -> Self {
        DescriptorState::Reserved(Reservation {
            is_oneway,
            free_res,
        })
    }
}

struct Reservation {
    is_oneway: bool,
    free_res: FreeNodeRes,
}

impl Reservation {
    fn allocate<T>(self, data: Option<T>) -> Allocation<T> {
        Allocation {
            data,
            is_oneway: self.is_oneway,
            free_res: self.free_res,
        }
    }
}

struct Allocation<T> {
    is_oneway: bool,
    free_res: FreeNodeRes,
    data: Option<T>,
}

impl<T> Allocation<T> {
    fn deallocate(self) -> (Reservation, Option<T>) {
        (
            Reservation {
                is_oneway: self.is_oneway,
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

// TODO: uncomment once test support is available
// #[kunit_tests(range_alloc)]
#[cfg(test)] // TODO: remove once test support is available
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
        let mut expected: &[Option<DescriptorState<usize>>] = &[None];
        assert_invariant_and_state(&mut ra, expected);

        let offset = ra
            .reserve_new(4, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        expected = &[Some(reserved(false)), None];
        assert_eq!(offset, 0);
        assert_invariant_and_state(&mut ra, expected);

        let offset = ra
            .reserve_new(5, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        expected = &[Some(reserved(false)), Some(reserved(false)), None];
        assert_eq!(offset, 4);
        assert_invariant_and_state(&mut ra, expected);

        let offset = ra
            .reserve_new(1, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        expected = &[
            Some(reserved(false)),
            Some(reserved(false)),
            Some(reserved(false)),
        ];
        assert_eq!(offset, 9);
        assert_invariant_and_state(&mut ra, expected);

        let offset = ra.reserve_new(2, false, ReserveNewBox::try_new().unwrap());
        assert!(offset.is_err());
    }

    #[test]
    fn test_reserve_new_noalloc() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[None];
        assert_invariant_and_state(&mut ra, expected);

        let mut offset = ra
            .reserve_new(4, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        expected = &[Some(reserved(false)), None];
        assert_eq!(offset, 0);
        assert_invariant_and_state(&mut ra, expected);

        assert!(ra.reserve_new_noalloc(5, false).unwrap().is_none());
        expected = &[Some(reserved(false)), Some(reserved(false))];
        offset = ra.reserve_new_noalloc(6, false).unwrap().unwrap();
        assert_eq!(offset, 4);
        assert_invariant_and_state(&mut ra, expected);
    }

    #[test]
    fn test_reservation_abort_no_merge() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[
            Some(reserved(false)),
            Some(reserved(false)),
            Some(reserved(false)),
            None,
        ];
        ra.reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        ra.reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(offset_middle).unwrap();
        expected = &[Some(reserved(false)), None, Some(reserved(false)), None];

        assert_invariant_and_state(&mut ra, expected);
    }

    #[test]
    fn test_reservation_abort_merge_right() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[
            Some(reserved(false)),
            Some(reserved(false)),
            Some(reserved(false)),
            None,
        ];
        ra.reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        ra.reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_right = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(offset_right).unwrap();
        expected = &[Some(reserved(false)), Some(reserved(false)), None];

        assert_invariant_and_state(&mut ra, expected);
    }

    #[test]
    fn test_reservation_abort_merge_left() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[
            Some(reserved(false)),
            Some(reserved(false)),
            Some(reserved(false)),
            None,
        ];
        let offset_left = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        ra.reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(offset_left).unwrap();
        expected = &[None, Some(reserved(false)), Some(reserved(false)), None];

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(offset_middle).unwrap();
        expected = &[None, Some(reserved(false)), None];

        assert_invariant_and_state(&mut ra, expected);
    }

    #[test]
    fn test_reservation_abort_merge_both() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[
            Some(reserved(false)),
            Some(reserved(false)),
            Some(reserved(false)),
            None,
        ];
        let offset_left = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_right = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(offset_left).unwrap();
        ra.reservation_abort(offset_right).unwrap();
        expected = &[None, Some(reserved(false)), None];

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(offset_middle).unwrap();
        expected = &[None];

        assert_invariant_and_state(&mut ra, expected);
    }

    #[test]
    fn test_reservation_commit() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[
            Some(reserved(false)),
            Some(reserved(false)),
            Some(reserved(false)),
            None,
        ];
        let offset_left = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_right = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(offset_left).unwrap();
        ra.reservation_abort(offset_right).unwrap();
        expected = &[None, Some(reserved(false)), None];

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_commit(offset_middle, Some(1)).unwrap();
        expected = &[None, Some(allocated(false)), None];

        assert_invariant_and_state(&mut ra, expected);
    }

    #[test]
    fn test_reserve_existing() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(10).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[
            Some(reserved(false)),
            Some(reserved(false)),
            Some(reserved(false)),
            None,
        ];
        let offset_left = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_middle = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let offset_right = ra
            .reserve_new(2, false, ReserveNewBox::try_new().unwrap())
            .unwrap();

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(offset_left).unwrap();
        ra.reservation_abort(offset_right).unwrap();
        expected = &[None, Some(reserved(false)), None];

        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_commit(offset_middle, Some(1)).unwrap();
        expected = &[None, Some(allocated(false)), None];

        assert_invariant_and_state(&mut ra, expected);

        let existing = ra.reserve_existing(offset_middle).unwrap();
        assert_eq!(existing, (2, Some(1)));
        expected = &[None, Some(reserved(false)), None];

        assert_invariant_and_state(&mut ra, expected);
    }

    #[test]
    fn test_end_to_end() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(1040384).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[None];
        assert_invariant_and_state(&mut ra, expected);

        assert!(ra.reserve_new_noalloc(16, false).unwrap().is_none());

        let offset = ra
            .reserve_new(16, false, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[Some(reserved(false)), None];
        assert_eq!(offset, 0);
        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_commit(0, Some(1)).unwrap();
        expected = &[Some(allocated(false)), None];
        assert_invariant_and_state(&mut ra, expected);

        let existing = ra.reserve_existing(offset).unwrap();
        assert_eq!(existing, (16, Some(1)));

        expected = &[Some(reserved(false)), None];
        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(0).unwrap();
        expected = &[None];
        assert_invariant_and_state(&mut ra, expected);
    }

    #[test]
    fn test_end_to_end_oneway() {
        let mut ra: RangeAllocator<usize> = RangeAllocator::new(1040384).unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[None];
        assert_invariant_and_state(&mut ra, expected);

        assert!(ra.reserve_new_noalloc(16, true).unwrap().is_none());

        let offset = ra
            .reserve_new(16, true, ReserveNewBox::try_new().unwrap())
            .unwrap();
        let mut expected: &[Option<DescriptorState<usize>>] = &[Some(reserved(true)), None];
        assert_eq!(offset, 0);
        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_commit(0, Some(1)).unwrap();
        expected = &[
            Some(DescriptorState::Allocated(
                Reservation::new(true, RBTree::try_reserve_node().unwrap()).allocate(Some(1)),
            )),
            None,
        ];
        assert_invariant_and_state(&mut ra, expected);

        let existing = ra.reserve_existing(offset).unwrap();
        assert_eq!(existing, (16, Some(1)));

        expected = &[Some(reserved(true)), None];
        assert_invariant_and_state(&mut ra, expected);

        let offset = ra
            .reserve_new(16, true, ReserveNewBox::try_new().unwrap())
            .unwrap();
        expected = &[Some(reserved(true)), Some(reserved(true)), None];
        assert_eq!(offset, 16);
        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_commit(offset, Some(1)).unwrap();
        expected = &[
            Some(reserved(true)),
            Some(DescriptorState::Allocated(
                Reservation::new(true, RBTree::try_reserve_node().unwrap()).allocate(Some(1)),
            )),
            None,
        ];
        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_commit(0, Some(1)).unwrap();
        expected = &[
            Some(DescriptorState::Allocated(
                Reservation::new(true, RBTree::try_reserve_node().unwrap()).allocate(Some(1)),
            )),
            Some(DescriptorState::Allocated(
                Reservation::new(true, RBTree::try_reserve_node().unwrap()).allocate(Some(1)),
            )),
            None,
        ];
        assert_invariant_and_state(&mut ra, expected);

        let existing = ra.reserve_existing(0).unwrap();
        assert_eq!(existing, (16, Some(1)));

        expected = &[
            Some(reserved(true)),
            Some(DescriptorState::Allocated(
                Reservation::new(true, RBTree::try_reserve_node().unwrap()).allocate(Some(1)),
            )),
            None,
        ];
        assert_invariant_and_state(&mut ra, expected);

        let existing = ra.reserve_existing(16).unwrap();
        assert_eq!(existing, (16, Some(1)));

        expected = &[Some(reserved(true)), Some(reserved(true)), None];
        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(0).unwrap();
        expected = &[None, Some(reserved(true)), None];
        assert_invariant_and_state(&mut ra, expected);

        ra.reservation_abort(16).unwrap();
        expected = &[None];
        assert_invariant_and_state(&mut ra, expected);
    }

    fn assert_invariant_and_state(
        ra: &mut RangeAllocator<usize>,
        expected: &[Option<DescriptorState<usize>>],
    ) {
        let cursor: RBTreeCursor<'_, usize, Descriptor<usize>> = ra.tree.cursor_front().unwrap();
        let free_tree: &RBTree<(usize, usize), ()> = &ra.free_tree;
        let free_oneway_space = ra.free_oneway_space;

        let mut index = 1;
        let (key, desc) = cursor.current();

        assert_eq!(key, &desc.offset);
        assert!(equal(&expected[0], &desc.state));

        let mut total_space = desc.size;
        let mut consumed_oneway_space = 0;

        match desc.state {
            // free descriptors should always have corresponding entries in the free tree
            None => {
                assert!(free_tree.get(&(desc.size, desc.offset)).is_some());
            }
            // oneway descriptors consume oneway space
            Some(DescriptorState::Reserved(Reservation { is_oneway, .. })) if is_oneway => {
                consumed_oneway_space += desc.size;
            }
            Some(DescriptorState::Allocated(Allocation { is_oneway, .. })) if is_oneway => {
                consumed_oneway_space += desc.size;
            }
            _ => {}
        }

        let mut last = (desc.offset, desc.size, clone(&desc.state));
        let mut next = cursor.move_next();

        while let Some(n) = next {
            let (key, desc) = n.current();
            let (last_offset, last_size, last_state) = last;

            assert_eq!(key, &desc.offset);
            assert!(equal(&expected[index], &desc.state));

            total_space += desc.size;

            match desc.state {
                // free descriptors should always have corresponding entries in the free tree
                None => {
                    assert!(free_tree.get(&(desc.size, desc.offset)).is_some());
                }
                // oneway descriptors consume oneway space
                Some(DescriptorState::Reserved(Reservation { is_oneway, .. })) if is_oneway => {
                    consumed_oneway_space += desc.size;
                }
                Some(DescriptorState::Allocated(Allocation { is_oneway, .. })) if is_oneway => {
                    consumed_oneway_space += desc.size;
                }
                _ => {}
            }

            // any descriptor's offset should always be a function of it's predecessors offset + size
            assert_eq!(desc.offset, last_offset + last_size);

            // adjacent descriptors should never both be free (as they should have been merged)
            assert!(last_state.is_some() || desc.state.is_some());

            last = (desc.offset, desc.size, clone(&desc.state));
            index += 1;
            next = n.move_next();
        }

        assert!(expected.len() == index);

        // free_oneway_space = (total_space / 2) - consumed_oneway_space
        assert_eq!(
            free_oneway_space,
            ((total_space / 2) - consumed_oneway_space)
        );

        // the free tree should not have extra entries
        let expected_free_count = expected.iter().filter(|e| e.is_none()).count();

        let actual_free_count = free_tree.iter().count();

        assert_eq!(expected_free_count, actual_free_count);
    }

    fn reserved(is_oneway: bool) -> DescriptorState<usize> {
        DescriptorState::new(is_oneway, RBTree::try_reserve_node().unwrap())
    }

    fn allocated(is_oneway: bool) -> DescriptorState<usize> {
        DescriptorState::Allocated(Allocation {
            is_oneway,
            free_res: RBTree::try_reserve_node().unwrap(),
            data: Some(1),
        })
    }

    fn clone(input: &Option<DescriptorState<usize>>) -> Option<DescriptorState<usize>> {
        match input {
            None => None,
            Some(state) => Some(match state {
                DescriptorState::Reserved(Reservation { is_oneway, .. }) => reserved(*is_oneway),
                DescriptorState::Allocated(Allocation { is_oneway, .. }) => allocated(*is_oneway),
            }),
        }
    }

    fn equal(lhs: &Option<DescriptorState<usize>>, rhs: &Option<DescriptorState<usize>>) -> bool {
        match (lhs, rhs) {
            (None, None) => true,
            (Some(lhs), Some(rhs)) => match (lhs, rhs) {
                (DescriptorState::Reserved(lhs), DescriptorState::Reserved(rhs)) => {
                    lhs.is_oneway == rhs.is_oneway
                }
                (DescriptorState::Allocated(lhs), DescriptorState::Allocated(rhs)) => {
                    if lhs.is_oneway != rhs.is_oneway {
                        return false;
                    }
                    match (lhs.data, rhs.data) {
                        (None, None) => true,
                        (Some(_), Some(_)) => true,
                        _ => false,
                    }
                }
                _ => false,
            },
            _ => false,
        }
    }
}
