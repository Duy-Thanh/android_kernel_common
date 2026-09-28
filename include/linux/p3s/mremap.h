/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_MREMAP_H
#define _LINUX_P3S_MREMAP_H

#include <linux/p3s/vma.h>
#include <linux/err.h>

#ifndef MREMAP_FIXED
#define MREMAP_FIXED 2
#endif

struct file;
unsigned long __get_unmapped_area(struct file *file, unsigned long addr,
				  unsigned long len, unsigned long pgoff,
				  unsigned long flags, vm_flags_t vm_flags);

/**
 * mm_mremap_get_unmapped_area - Find an mremap() target that keeps the slice
 * @file: Backing file (if any)
 * @addr: Target address / user hint
 * @len: Length of the new mapping
 * @pgoff: Page offset in the file / mapping
 * @map_flags: MAP_* flags for get_unmapped_area
 * @old_addr: Source address being remapped
 * @mremap_flags: MREMAP_* flags (e.g. MREMAP_FIXED, MREMAP_DONTUNMAP)
 *
 * In a 4KB process, mremap() moves the PTEs as they are, so each one still
 * maps the slice that the old address used. Later faults, COW, migration
 * and sibling lookaround compute the slice from the new address
 * (vma_slice_offset()), so the new range must start at the same slice of
 * a 16KB-aligned window as @old_addr.
 *
 * Without MREMAP_FIXED, ask __get_unmapped_area() for an area of
 * @len + PAGE_SIZE_KERNEL bytes. In a 4KB process its start, res, is 16KB
 * aligned, so return res plus @old_addr's slice offset. For example, if
 * @old_addr is 0x1000 bytes into its window, it is at slice
 * (@old_addr >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK = 1, and the new range
 * starts at res + (1 << PAGE_SHIFT_4KB) = res + 0x1000:
 *
 *                @old_addr                    res + 0x1000
 *                    ▼                              ▼
 *                    ┌─────────────────┐            ┌─────────────────┐
 *  ranges            │       old       │            │       new       │
 *                    └─────────────────┘            └─────────────────┘
 *              ┌─────┬─────┬─────┬─────┐      ┌─────┬─────┬─────┬─────┐
 *  slices      │  0  │  1  │  2  │  3  │ ...  │  0  │  1  │  2  │  3  │
 *              └─────┴─────┴─────┴─────┘      └─────┴─────┴─────┴─────┘
 *                                             ▲
 *                                             └─ res
 *
 * A destination hint from MREMAP_DONTUNMAP (@addr) is used as is when free.
 * A cross-slice anonymous move is repacked by p3s_mremap_reslice().
 *
 * With MREMAP_FIXED, keep the requested target. If an anonymous mapping
 * changes its slice alignment, p3s_mremap_prepare() rejects pinned source
 * folios and p3s_mremap_reslice() copies their live slices to new folios.
 */
static inline unsigned long mm_mremap_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long map_flags, unsigned long old_addr,
		unsigned long mremap_flags)
{
	if (mm_is_p3s_4k(current->mm)) {
		unsigned int slice =
			(old_addr >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK;

		if (!(mremap_flags & MREMAP_FIXED) && (slice || addr)) {
			unsigned long res;

			/*
			 * MREMAP_DONTUNMAP treats a non-zero destination as an
			 * mmap-style hint. Use an available hint as is; the
			 * cross-slice move path will repack anonymous PTEs.
			 * Otherwise add padding to preserve the source slice.
			 */
			if (addr) {
				res = __get_unmapped_area(file, addr, len,
							  pgoff, map_flags, 0);
				if (res == addr || IS_ERR_VALUE(res))
					return res;
			}

			res = __get_unmapped_area(file, addr & PAGE_MASK_KERNEL,
						  len + PAGE_SIZE_KERNEL,
						  pgoff, map_flags, 0);
			if (!IS_ERR_VALUE(res))
				res += (unsigned long)slice << PAGE_SHIFT_4KB;
			return res;
		}
	}

	return __get_unmapped_area(file, addr, len, pgoff, map_flags, 0);
}

#endif /* _LINUX_P3S_MREMAP_H */
