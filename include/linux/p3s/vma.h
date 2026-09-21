/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_VMA_H
#define _LINUX_P3S_VMA_H

#include <linux/p3s/mm.h>
#include <linux/minmax.h>

struct vm_area_struct;

/*
 * VMA Geometry Abstractions:
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ 16KB Host Folio (Kernel Page)                           │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Slice 0   │   Slice 1   │   Slice 2   │    Slice 3    │
 * │   (4 KB)    │   (4 KB)    │   (4 KB)    │    (4 KB)     │
 * └─────────────┴─────────────┴─────────────┴───────────────┘
 *               ▲                           ▲
 *               ├─── vma->vm_start          └─── vma->vm_end
 *               │    (Slice 1)                   (Slice 3)
 *               │
 *               ◄─────────── vma_size: 8 KB ────────►
 */

/*
 * vma_size - Virtual address byte span of a VMA
 * @vma: Pointer to struct vm_area_struct
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ VMA Virtual Address Span                                │
 * ├────────────────────────────┬────────────────────────────┤
 * │ vma->vm_start              │ vma->vm_end                │
 * └────────────────────────────┴────────────────────────────┘
 * ◄────────────────────── vma_size ─────────────────────────►
 */
static inline unsigned long vma_size(const struct vm_area_struct *vma)
{
	return vma->vm_end - vma->vm_start;
}

/*
 * vma_page_size - Virtual page size in bytes of the process owning the VMA
 * @vma: Pointer to struct vm_area_struct
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ Process Page Size                                       │
 * ├────────────────────────────┬────────────────────────────┤
 * │ Compat Process: 4096 bytes │ Native Process: 16384 bytes│
 * └────────────────────────────┴────────────────────────────┘
 */
static inline unsigned long vma_page_size(const struct vm_area_struct *vma)
{
	return mm_pte_size(vma->vm_mm);
}

static inline unsigned long vma_page_mask(const struct vm_area_struct *vma)
{
	return mm_pte_mask(vma->vm_mm);
}

static inline unsigned long vma_phys_pfn(const struct vm_area_struct *vma,
					phys_addr_t x)
{
	return mm_phys_pfn(vma->vm_mm, x);
}

/*
 * vma_is_compat - Check if VMA belongs to a 4KB compatibility process
 * @vma: Pointer to struct vm_area_struct
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ Process Execution Mode                                  │
 * ├────────────────────────────┬────────────────────────────┤
 * │ Compat (TCR TG0 = 4KB)     │ Native (TCR TG0 = 16KB)    │
 * └────────────────────────────┴────────────────────────────┘
 */
static inline bool vma_is_compat(const struct vm_area_struct *vma)
{
	return mm_is_compat(vma->vm_mm);
}

/*
 * vma_offset_in_page - Calculate byte offset of address within process page
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ Process Virtual Page (4KB or 16KB)                      │
 * ├─────────────────────────────────────────────────────────┤
 * │ ◄── vma_offset_in_page ──► ▲                            │
 * │                            └── addr                     │
 * └─────────────────────────────────────────────────────────┘
 */
static inline unsigned long vma_offset_in_page(const struct vm_area_struct *vma,
					       unsigned long addr)
{
	return mm_offset_in_page(vma->vm_mm, addr);
}

#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE
#define p3s_adjust_unmapped_area_info(info) do { \
	if (!(info)->align_mask && current->mm && mm_is_compat(current->mm)) \
		(info)->align_mask = ~PAGE_MASK_KERNEL; \
} while (0)
#else
#define p3s_adjust_unmapped_area_info(info) ((void)(info))
#endif

#endif /* _LINUX_P3S_VMA_H */
