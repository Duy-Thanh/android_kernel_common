/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_FUTEX_H
#define _LINUX_P3S_FUTEX_H

#include <linux/mm.h>
#include <linux/mmap_lock.h>

/*
 * futex_page_offset - Offset of a shared futex within its page of the file
 * @mm: mm_struct of the task using the futex
 * @uaddr: User address of the futex
 * @offset: Offset of @uaddr within its kernel page
 *
 * Inode based futex keys combine the index of the page that backs a futex
 * with the offset of the futex in that page. A 4KB compatibility VMA can
 * map a file at any 4KB offset, so the address does not determine that
 * offset: take the slice from the VMA. Returns @offset for other mms.
 */
static inline unsigned long futex_page_offset(struct mm_struct *mm,
					      unsigned long uaddr,
					      unsigned long offset)
{
	struct vm_area_struct *vma;

	if (!mm_is_p3s_4k(mm))
		return offset;

	uaddr = untagged_addr(uaddr);
	mmap_read_lock(mm);
	vma = vma_lookup(mm, uaddr);
	if (vma)
		offset = vma_folio_offset(vma, uaddr);
	mmap_read_unlock(mm);
	return offset;
}

#endif /* _LINUX_P3S_FUTEX_H */
