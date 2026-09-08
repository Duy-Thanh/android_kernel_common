// SPDX-License-Identifier: GPL-2.0
/*
 * Range-oriented P3S GUP interfaces. This file is included by gup.c so the
 * range wrappers can use its private walkers without exporting internals.
 */

static long gup_user_range(struct mm_struct *mm, unsigned long start,
			   size_t length, unsigned long capacity,
			   unsigned int gup_flags, struct page **pages,
			   struct page_span *spans,
			   bool pin)
{
	unsigned long nr_pages = mm_user_range_pages(mm, start, length);
	unsigned long addr, end, page_size = mm_pte_size(mm);
	bool compat = mm_is_p3s_4k(mm);
	bool locked = false;
	size_t remaining = length;
	long ret, i;

	if (!length)
		return 0;
	if (nr_pages > capacity)
		return -ENOSPC;
	if (nr_pages > INT_MAX)
		return -EOVERFLOW;
	if (!pages || !spans)
		return -EINVAL;

	/* The native local-mm path retains fast GUP. */
	if (mm == current->mm && !compat) {
		start = untagged_addr(start);
	} else {
		mmap_read_lock(mm);
		locked = true;
		start = untagged_addr_remote(mm, start);
	}
	if (check_add_overflow(start, length, &end)) {
		ret = -EOVERFLOW;
		goto out;
	}
	if (compat) {
		int lock_state = 1;
		unsigned int flags = gup_flags | FOLL_TOUCH |
				(pin ? FOLL_PIN : FOLL_GET);

		if (mm != current->mm)
			flags |= FOLL_REMOTE;
		if (pin)
			mm_set_has_pinned_flag(mm);
		if (pin && (flags & FOLL_LONGTERM)) {
			unsigned int memflags = memalloc_pin_save();
			long migration;

			do {
				ret = __get_user_pages(mm, start, nr_pages,
						       flags, pages, &lock_state,
						       spans);
				migration = ret > 0 ?
					check_and_migrate_movable_pages(ret, pages) : 0;
			} while (migration == -EAGAIN);
			memalloc_pin_restore(memflags);
			if (migration)
				ret = migration;
		} else {
			ret = __get_user_pages(mm, start, nr_pages, flags,
					       pages, &lock_state, spans);
		}
	} else if (locked) {
		ret = pin ? pin_user_pages_remote(mm, start, nr_pages,
						  gup_flags, pages, NULL) :
			    get_user_pages_remote(mm, start, nr_pages,
						  gup_flags, pages, NULL);
	} else {
		ret = pin ? pin_user_pages_fast(start, nr_pages, gup_flags,
						pages) :
			    get_user_pages_fast(start, nr_pages, gup_flags,
						pages);
	}

	addr = start;
	for (i = 0; i < ret; i++) {
		unsigned int in_page = addr & (page_size - 1);
		unsigned int bytes =
			min_t(size_t, remaining, page_size - in_page);

		if (!compat)
			spans[i].offset = in_page;
		spans[i].length = bytes;
		VM_WARN_ON_ONCE(spans[i].offset + bytes > PAGE_SIZE_KERNEL);
		addr += bytes;
		remaining -= bytes;
	}
out:
	if (locked)
		mmap_read_unlock(mm);
	return ret;
}

/**
 * pin_user_pages_range() - pin a byte range and describe its native backing
 * @mm: target address space
 * @start: first user byte (need not be page aligned)
 * @length: number of bytes requested
 * @capacity: number of entries available in both output arrays
 * @gup_flags: GUP flags, including long-term/write requirements
 * @pages: native pages, one pin per returned entry (duplicates are allowed)
 * @spans: byte offset and valid length in the corresponding @pages entry
 *
 * Allocations can use mm_user_range_pages() to size the arrays. Takes
 * mmap_read_lock() as needed; callers must not hold it. A positive return
 * is the number of entries, possibly short, not a byte count. Only those
 * entries are valid and each must be unpinned, even if page pointers repeat.
 * The sum of their span lengths is the completed byte count. No merging or
 * pin deduplication is performed. Errors and zero-length requests own no pins.
 */
long pin_user_pages_range(struct mm_struct *mm, unsigned long start,
			  size_t length, unsigned long capacity,
			  unsigned int gup_flags, struct page **pages,
			  struct page_span *spans)
{
	return gup_user_range(mm, start, length, capacity, gup_flags, pages,
			      spans, true);
}
EXPORT_SYMBOL_GPL(pin_user_pages_range);

/**
 * get_user_pages_range() - get references to a byte range's native backing
 * @mm: target address space
 * @start: first user byte
 * @length: number of bytes requested
 * @capacity: number of entries available in both output arrays
 * @gup_flags: GUP flags
 * @pages: native pages, one reference per returned entry
 * @spans: byte offset and valid length for each returned page
 *
 * Like pin_user_pages_range(), but release each returned reference with
 * put_page()/release_pages(), not unpin_user_pages().
 */
long get_user_pages_range(struct mm_struct *mm, unsigned long start,
			  size_t length, unsigned long capacity,
			  unsigned int gup_flags, struct page **pages,
			  struct page_span *spans)
{
	return gup_user_range(mm, start, length, capacity, gup_flags, pages,
			      spans, false);
}
EXPORT_SYMBOL_GPL(get_user_pages_range);
