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
 * vma_linear_page_index - Map virtual address to host folio index
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * Translates a 4KB virtual address to the enclosing 16KB host folio index
 * in the file Page Cache (or 4KB page index for anonymous VMAs):
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ 16KB Page Cache Folio (vm_pgoff = N)                    │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Slice 0   │   Slice 1   │   Slice 2   │    Slice 3    │
 * └─────────────┴─────────────┴─────────────┴───────────────┘
 *               ▲                           ▲
 *               ├── vma->vm_start (off=1)   └── addr (off=3)
 *               └───────────────────────────────► returns folio N
 */
static inline pgoff_t vma_linear_page_index(const struct vm_area_struct *vma,
					    unsigned long addr)
{
	if (!vma_is_compat(vma) || (vma->vm_ops && !vma->vm_file))
		return vma->vm_pgoff +
		       ((addr - vma->vm_start) >> PAGE_SHIFT_KERNEL);

	if (!vma->vm_ops)
		return vma->vm_pgoff +
		       ((addr - vma->vm_start) >> PAGE_SHIFT_4KB);

	return vma->vm_pgoff +
	       ((vma_slice_off(vma) + ((addr - vma->vm_start) >> PAGE_SHIFT_4KB)) >>
		P3S_SLICE_SHIFT);
}

/*
 * vma_pgoff_offset - Translate virtual address to VMA page offset
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * Assumes addr >= vma->vm_start.
 */
static inline pgoff_t vma_pgoff_offset(const struct vm_area_struct *vma,
				       unsigned long addr)
{
	return vma_linear_page_index(vma, addr);
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

/*
 * vma_pgoff_to_address - Convert Page Cache pgoff to virtual address
 * @vma: Pointer to struct vm_area_struct
 * @pgoff: Page offset in host 16KB folio units
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ Backing File Page Cache (16KB Folios)                   │
 * ├───────────────────────────┬─────────────────────────────┤
 * │ Folio 0 (pgoff = 0)       │ Folio 1 (pgoff = 1)         │
 * │ [ S0 │ S1 │ S2 │ S3 ]     │ [ S0 │ S1 │ S2 │ S3 ]       │
 * └───────────────────────────┴─────────────────────────────┘
 *                                    ▲
 *                                    └── pgoff=1, vma_slice_off=1
 *                                        maps to vma->vm_start
 */
static inline unsigned long
vma_pgoff_to_address(const struct vm_area_struct *vma, pgoff_t pgoff)
{
	unsigned long addr;

	if (pgoff <= vma->vm_pgoff)
		return vma->vm_start;

	if (vma->vm_file) {
		addr = vma->vm_start +
			((pgoff - vma->vm_pgoff) << PAGE_SHIFT_KERNEL) -
			((unsigned long)vma_slice_off(vma) << PAGE_SHIFT_4KB);
	} else {
		addr = vma->vm_start +
			((pgoff - vma->vm_pgoff) << mm_pte_shift(vma->vm_mm));
	}

	return min(addr, vma->vm_end);
}

/*
 * VMA Merge Struct Geometry Abstractions (vmg)
 */
#define vmg_is_compat(vmg)	mm_is_compat((vmg)->mm)

/*
 * vma_can_merge_offsets - Verify offset & slice continuity across adjacent VMAs
 * @left: Left (lower-address) VMA
 * @right_pgoff: Page offset of right (higher-address) range
 * @right_slice: Starting subpage slice of right range (0..3)
 * @boundary: Virtual address where left ends and right begins
 * @is_compat: Whether process is running in 4KB compat mode
 * @is_file: Whether mapping is file-backed
 *
 * ┌───────────────────────────┬───────────────────────────┐
 * │ Left Range (lower addr)   │ Right Range (higher addr) │
 * ├───────────────────────────┼───────────────────────────┤
 * │ left->vm_start            │ right->vm_start           │
 * │              left->vm_end │ (right_pgoff, right_slice)│
 * └───────────────────────────┴───────────────────────────┘
 *                             ▲
 *                             └── Boundary: boundary == right->vm_start
 *
 * ┌───────────────────────────┬───────────────────────────┐
 * │ Host Folio N (Left End)   │ Host Folio N+1 (Right)    │
 * ├──────┬──────┬──────┬──────┼──────┬──────┬──────┬──────┤
 * │  S0  │  S1  │  S2  │  S3  │  S0  │  S1  │  S2  │  S3  │
 * └──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────┘
 *                             ▲
 *                             └── linear_page_index(left, boundary) == right_pgoff
 *                             └── slice_offset(left, boundary) == right_slice
 */
static inline bool vma_can_merge_offsets(const struct vm_area_struct *left,
					 pgoff_t right_pgoff,
					 unsigned short right_slice,
					 unsigned long boundary,
					 bool is_compat, bool is_file)
{
	return left && vma_linear_page_index(left, boundary) == right_pgoff &&
	       (!is_compat || !is_file ||
		vma_slice_offset(left, boundary) == right_slice);
}

/*
 * vma_unmap_mapping_range_bounds - Calculate address bounds for unmapping
 * @vma: Pointer to struct vm_area_struct
 * @first_index: Start folio index
 * @last_index: End folio index
 * @start: Pointer to output start address
 * @end: Pointer to output end address
 */
static inline void vma_unmap_mapping_range_bounds(const struct vm_area_struct *vma,
						  pgoff_t first_index,
						  pgoff_t last_index,
						  unsigned long *start,
						  unsigned long *end)
{
	const pgoff_t start_idx = max(first_index, vma->vm_pgoff);
	const pgoff_t end_idx = min(last_index,
				    vma->vm_pgoff + vma_native_pages(vma) - 1) + 1;

	*start = vma_pgoff_to_address(vma, start_idx);
	*end = vma_pgoff_to_address(vma, end_idx);
}

/*
 * vma_remote_access_chunk - Compute folio offset and clamped length for remote access
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address being accessed
 * @len: Maximum bytes to transfer
 * @offset: Pointer to output byte offset within host folio
 * @bytes: Pointer to output transferable byte count within virtual page
 */
static inline void vma_remote_access_chunk(const struct vm_area_struct *vma,
					   unsigned long addr, int len,
					   int *offset, int *bytes)
{
	*offset = vma_folio_offset(vma, addr);
	*bytes = min_t(int, len, vma_page_size(vma) - vma_offset_in_page(vma, addr));
}

/* Forward declarations for VMA lifecycle operations */
int anon_vma_clone(struct vm_area_struct *, struct vm_area_struct *);

/*
 * vma_set_split_offset - Update pgoff and slice offset after VMA split
 * @target: Newly created or adjusted VMA
 * @src: Source VMA being split
 * @addr: Split boundary address
 */
static inline void vma_set_split_offset(struct vm_area_struct *target,
					const struct vm_area_struct *src,
					unsigned long addr)
{
	target->vm_pgoff = vma_linear_page_index(src, addr);
	vma_set_slice_off(target, vma_slice_offset(src, addr));
}

/*
 * vma_set_range_slice - Set VMA range and update subpage slice offset
 * @vma: Pointer to struct vm_area_struct
 * @start: Starting virtual address
 * @end: Ending virtual address
 * @pgoff: Page offset
 */
static inline void vma_set_range_slice(struct vm_area_struct *vma,
				       unsigned long start, unsigned long end,
				       pgoff_t pgoff)
{
	vma->vm_start = start;
	vma->vm_end = end;
	vma->vm_pgoff = pgoff;
	vma_set_slice_off(vma, vma_slice_offset(vma, start));
}

/*
 * vma_expand_downwards_range - Adjust start, pgoff, and slice offset for downward stack expansion
 * @vma: Pointer to struct vm_area_struct
 * @address: New lower virtual address
 * @grow: Number of pages to grow downwards
 */
static inline void vma_expand_downwards_range(struct vm_area_struct *vma,
					     unsigned long address,
					     unsigned long grow)
{
	vma->vm_start = address;
	vma->vm_pgoff -= grow;
	vma_set_slice_off(vma, 0);
}

/*
 * copy_vma_set_range - Set range and slice offset for duplicated VMA
 * @new_vma: Duplicated target VMA
 * @vma: Original source VMA
 * @addr: Starting virtual address
 * @len: Length of range
 * @pgoff: Page offset
 * @faulted_in_anon_vma: Whether source VMA had faulted anon_vma
 */
static inline void copy_vma_set_range(struct vm_area_struct *new_vma,
				      const struct vm_area_struct *vma,
				      unsigned long addr, unsigned long len,
				      pgoff_t pgoff, bool faulted_in_anon_vma)
{
	new_vma->vm_start = addr;
	new_vma->vm_end = addr + len;
	new_vma->vm_pgoff = pgoff;
	vma_set_slice_off(new_vma, faulted_in_anon_vma ? vma_slice_offset(vma, addr) : 0);
}

/*
 * vma_filter_merge_neighbor - Invalidate neighbor reference if it points to self
 * @neighbor: Potential neighbor VMA to merge with
 * @vma: Source VMA being copied
 * @faulted_in_anon_vma: Whether source VMA had faulted anon_vma
 */
static inline struct vm_area_struct *
vma_filter_merge_neighbor(struct vm_area_struct *neighbor,
			  const struct vm_area_struct *vma,
			  bool faulted_in_anon_vma)
{
	return (faulted_in_anon_vma && neighbor == vma) ? NULL : neighbor;
}

/*
 * copy_vma_clone_anon_vma - Clone anon_vma into merged target VMA if needed
 * @new_vma: Merged target VMA
 * @vma: Original source VMA
 * @faulted_in_anon_vma: Whether source VMA had faulted anon_vma
 */
static inline int copy_vma_clone_anon_vma(struct vm_area_struct *new_vma,
					 struct vm_area_struct *vma,
					 bool faulted_in_anon_vma)
{
	if (faulted_in_anon_vma && !new_vma->anon_vma) {
		new_vma->anon_vma = vma->anon_vma;
		return anon_vma_clone(new_vma, vma);
	}
	return 0;
}

/*
 * p3s_adjust_unmapped_area_info - Adjust unmapped area alignment for compat MM
 * @info: Pointer to struct vm_unmapped_area_info
 *
 * For compat tasks with sub-page granularity, align unmapped area allocations
 * to host kernel page boundaries unless explicitly aligned.
 */
#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE
#define p3s_adjust_unmapped_area_info(info) do { \
	if (!(info)->align_mask && current->mm && mm_is_compat(current->mm)) \
		(info)->align_mask = ~PAGE_MASK_KERNEL; \
} while (0)
#else
#define p3s_adjust_unmapped_area_info(info) ((void)(info))
#endif

#endif /* _LINUX_P3S_VMA_H */
