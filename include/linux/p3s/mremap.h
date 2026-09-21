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
 * mm_mremap_get_unmapped_area - Find unmapped area for mremap with slice preservation
 * @file: Backing file (if any)
 * @addr: Target address / user hint
 * @len: Length of the new mapping
 * @pgoff: Page offset in the file / mapping
 * @map_flags: MAP_* flags for get_unmapped_area
 * @old_addr: Source address being remapped
 * @mremap_flags: MREMAP_* flags (e.g. MREMAP_FIXED, MREMAP_DONTUNMAP)
 *
 * For 4KB compat processes on a 16KB kernel, a VMA may start at a non-zero
 * subpage slice offset within a host 16KB folio ((old_addr >> 12) & 3 != 0).
 *
 * When mremap() moves a VMA without MREMAP_FIXED, the destination address
 * must preserve the identical subpage slice offset within its enclosing 16KB
 * boundary so that page cache indexing, physical page alignment, and subpage
 * slicing remain consistent between old and new mappings:
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ 16KB Host Folio                                         │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Slice 0   │   Slice 1   │   Slice 2   │    Slice 3    │
 * └─────────────┴─────────────┴─────────────┴───────────────┘
 *               ▲
 *               └── old_addr starts at Slice 1
 *                   relocated dest is padded and offset to Slice 1
 */
static inline unsigned long mm_mremap_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long map_flags, unsigned long old_addr,
		unsigned long mremap_flags)
{
	if (mm_is_compat(current->mm) &&
	    !(mremap_flags & MREMAP_FIXED)) {
		unsigned int slice =
			(old_addr >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK;

		if (slice) {
			unsigned long res;

			/*
			 * MREMAP_DONTUNMAP treats a non-zero destination as an
			 * mmap-style hint. Honor an available hint before adding
			 * padding to preserve the source address slice.
			 */
			if (addr) {
				res = __get_unmapped_area(file, addr, len,
							  pgoff, map_flags, 0);
				if (res == addr || IS_ERR_VALUE(res))
					return res;
			}

			res = __get_unmapped_area(file, addr,
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
