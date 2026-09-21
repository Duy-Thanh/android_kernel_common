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

/*
 * vma_slice_off - Retrieve subpage slice offset within the host folio
 * @vma: Pointer to struct vm_area_struct
 *
 * Tracks the starting 4KB subpage slice (0..3) within the host 16KB folio:
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ 16KB Host Folio (Physical Page)                         │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Slice 0   │   Slice 1   │   Slice 2   │    Slice 3    │
 * │   (off=0)   │   (off=1)   │   (off=2)   │    (off=3)    │
 * └─────────────┴─────────────┴─────────────┴───────────────┘
 *               ▲
 *               └─── vma->vm_start (vma_slice_off = 1)
 */
#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE
static inline unsigned short vma_slice_off(const struct vm_area_struct *vma)
{
	return vma_is_compat(vma) ? vma->vm_slice_off : 0;
}

/*
 * vma_set_slice_off - Set subpage slice offset within the host folio
 * @vma: Pointer to struct vm_area_struct
 * @slice_off: Starting slice index (0..3) within the host folio
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ 16KB Host Folio (Physical Page)                         │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Slice 0   │   Slice 1   │   Slice 2   │    Slice 3    │
 * └─────────────┴─────────────┴─────────────┴───────────────┘
 *               ▲
 *               └─── vma_set_slice_off(vma, 1)
 */
static inline void vma_set_slice_off(struct vm_area_struct *vma,
				     unsigned short slice_off)
{
	vma->vm_slice_off = slice_off;
}
#else
static inline unsigned short vma_slice_off(const struct vm_area_struct *vma)
{
	return 0;
}

static inline void vma_set_slice_off(struct vm_area_struct *vma,
				     unsigned short slice_off)
{
}
#endif

/*
 * vma_slice_offset - Compute host folio slice index for a virtual address
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ 16KB Host Folio                                         │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Slice 0   │   Slice 1   │   Slice 2   │    Slice 3    │
 * └─────────────┴─────────────┴─────────────┴───────────────┘
 *                             ▲
 *                             └─── addr (vma_slice_offset = 2)
 */
static inline unsigned int vma_slice_offset(const struct vm_area_struct *vma,
					    unsigned long addr)
{
	if (!vma_is_compat(vma))
		return 0;

	if (!vma->vm_ops)
		return (addr >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK;

	return (vma_slice_off(vma) + ((addr - vma->vm_start) >> PAGE_SHIFT_4KB)) &
	       P3S_SLICE_MASK;
}

/*
 * vma_folio_offset - Calculate byte offset within host folio for an address
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ 16KB Host Folio (Physical Page)                         │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Slice 0   │   Slice 1   │   Slice 2   │    Slice 3    │
 * │   (0..4K)   │   (4..8K)   │  (8..12K)   │   (12..16K)   │
 * └─────────────┴─────────────┴─────────────┴───────────────┘
 *                             ▲
 *                             └── folio offset for slice 2
 */
static inline unsigned long vma_folio_offset(const struct vm_area_struct *vma,
					     unsigned long addr)
{
	return ((unsigned long)vma_slice_offset(vma, addr) << PAGE_SHIFT_4KB) |
	       vma_offset_in_page(vma, addr);
}

/*
 * vma_file_offset - Calculate starting byte offset in backing file
 * @vma: Pointer to struct vm_area_struct
 *
 * Combines 16KB Page Cache folio index (vm_pgoff << 14) with subpage
 * slice offset (vm_slice_off << 12) for 4KB compat file mappings:
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ Backing File Page Cache (16KB Folios)                   │
 * ├───────────────────────────┬─────────────────────────────┤
 * │ Folio 0 (vm_pgoff = 0)    │ Folio 1 (vm_pgoff = 1)      │
 * │ [ S0 │ S1 │ S2 │ S3 ]     │ [ S0 │ S1 │ S2 │ S3 ]       │
 * └───────────────────────────┴─────────────────────────────┘
 *                                    ▲
 *                                    └── vm_pgoff=1, slice_off=1
 * ◄──────────────── vma_file_offset: 20 KB ────────────────►
 */
static inline loff_t vma_file_offset(const struct vm_area_struct *vma)
{
	return ((loff_t)vma->vm_pgoff << PAGE_SHIFT_KERNEL) +
	       ((loff_t)vma_slice_off(vma) << PAGE_SHIFT_4KB);
}

/*
 * vma_file_offset_at - Calculate byte offset in backing file for an address
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ Backing File Byte Stream                                │
 * ├─────────────────────────────────────────────────────────┤
 * │ ◄────── vma_file_offset ──────► ▲                       │
 * │ ◄────────────── vma_file_offset_at ──────────────►     │
 * └─────────────────────────────────────────────────────────┘
 */
static inline loff_t vma_file_offset_at(const struct vm_area_struct *vma,
					unsigned long addr)
{
	return vma_file_offset(vma) + (addr - vma->vm_start);
}

/*
 * vma_native_pages - Number of native 16KB folios spanned by VMA
 * @vma: Pointer to struct vm_area_struct
 *
 * Calculates the number of 16KB physical folios covered across
 * subpage slice boundaries:
 *
 * ┌───────────────────────────┬─────────────────────────────┐
 * │ Host Folio 0 (16KB)       │ Host Folio 1 (16KB)         │
 * ├─────┬─────┬───────┬───────┼───────┬─────┬───────┬───────┤
 * │ S0  │ S1  │  S2   │  S3   │  S0   │ S1  │  S2   │  S3   │
 * └─────┴─────┴───────┴───────┴───────┴─────┴───────┴───────┘
 *             ▲                               ▲
 *             ├── vma->vm_start (S2)          └── vma->vm_end (S1)
 *             ◄──────── vma_native_pages: 2 ────────►
 */
static inline pgoff_t vma_native_pages(const struct vm_area_struct *vma)
{
	unsigned long nr_slices = vma_size(vma) >> mm_pte_shift(vma->vm_mm);

	if (!vma_is_compat(vma) || !vma->vm_ops)
		return nr_slices;

	return DIV_ROUND_UP(vma_slice_off(vma) + nr_slices,
			    P3S_SLICES_PER_PAGE);
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
