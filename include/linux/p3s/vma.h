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

static inline unsigned long vma_size(const struct vm_area_struct *vma)
{
	return vma->vm_end - vma->vm_start;
}

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

static inline bool vma_is_p3s_4k(const struct vm_area_struct *vma)
{
	return mm_is_p3s_4k(vma->vm_mm);
}

static inline unsigned long vma_offset_in_page(const struct vm_area_struct *vma,
					       unsigned long addr)
{
	return mm_offset_in_page(vma->vm_mm, addr);
}

#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE
static inline unsigned short vma_slice_off(const struct vm_area_struct *vma)
{
	return vma_is_p3s_4k(vma) ? vma->vm_slice_off : 0;
}

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
 * vma_slice_offset - Slice of the 16KB page that backs @addr
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * Each 4KB page of a 4KB VMA is backed by one 4KB slice (0..3) of a 16KB
 * page. Returns the slice that backs @addr, or 0 for native VMAs.
 *
 * A file VMA maps its first page from slice vm_slice_off of file page
 * vm_pgoff and each following page from the next slice, so the slice is
 * the number of slices from the start of file page vm_pgoff to @addr,
 * modulo the slices per page.
 *
 * For example, with vm_slice_off = 1 and @addr = vm_start + 0x4000, @addr
 * is (0x4000 >> PAGE_SHIFT_4KB) = 4 pages after vm_start, or 1 + 4 = 5
 * slices after the start of file page vm_pgoff. So it is slice
 * 5 & P3S_SLICE_MASK = 1 of file page vm_pgoff + 1:
 *
 *                vm_start                     @addr
 *                    ▼                          ▼
 *                    ┌─────┬─────┬─────┬─────┬─────┐
 *  VMA pages         │  0  │  1  │  2  │  3  │  4  │
 *                    └─────┴─────┴─────┴─────┴─────┘
 *              ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 *  slices      │  0  │  1  │  2  │  3  │  0  │  1  │  2  │  3  │
 *              ├─────┴─────┴─────┴─────┼─────┴─────┴─────┴─────┤
 *  file pages  │        vm_pgoff       │      vm_pgoff + 1     │
 *              └───────────────────────┴───────────────────────┘
 *
 * An anonymous folio covers a 16KB-aligned window of virtual addresses,
 * so the slice is the index of @addr's 4KB page within its window.
 */
static inline unsigned int vma_slice_offset(const struct vm_area_struct *vma,
					    unsigned long addr)
{
	/* Native VMAs map whole kernel pages, so there is no slice. */
	if (!vma_is_p3s_4k(vma))
		return 0;

	/*
	 * An anonymous folio covers a 16KB-aligned window of virtual
	 * addresses, so the index of @addr's 4KB page in that window is its
	 * slice.
	 */
	if (!vma->vm_ops)
		return (addr >> PAGE_SHIFT_4KB) & P3S_SLICE_MASK;

	/*
	 * Other VMAs (files, shmem) map folios by offset: vm_start maps slice
	 * vm_slice_off of its folio. Count the 4KB pages from vm_start to
	 * @addr on top of that slice; the low bits are the slice of the folio
	 * that backs @addr.
	 */
	return (vma_slice_off(vma) + ((addr - vma->vm_start) >> PAGE_SHIFT_4KB)) &
	       P3S_SLICE_MASK;
}

/*
 * vma_folio_offset - Byte offset of @addr within the 16KB page backing it
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * This is the start of the slice that backs @addr plus @addr's offset
 * within its 4KB page. For example, for an address 0x234 bytes into a 4KB
 * page that slice 1 backs:
 *
 *              0x0000    0x1000    0x2000    0x3000    0x4000
 *              ┌─────────┬─────────┬─────────┬─────────┐
 *  16KB page   │ slice 0 │ slice 1 │ slice 2 │ slice 3 │
 *              └─────────┴─────────┴─────────┴─────────┘
 *                        ▲ ▲
 *                        │ └─ @addr is at 0x1000 + 0x234 = 0x1234
 *                        └─── slice 1 starts at 1 << PAGE_SHIFT_4KB = 0x1000
 *
 * For a native VMA the slice is 0, so this is @addr's offset within its
 * 16KB page.
 */
static inline unsigned long vma_folio_offset(const struct vm_area_struct *vma,
					     unsigned long addr)
{
	/*
	 * The slice shifted by PAGE_SHIFT_4KB is the start of @addr's 4KB
	 * page within the 16KB folio, and vma_offset_in_page() is @addr's
	 * offset within that page. Native VMAs have slice 0, which leaves the
	 * offset within the 16KB page.
	 */
	return ((unsigned long)vma_slice_offset(vma, addr) << PAGE_SHIFT_4KB) |
	       vma_offset_in_page(vma, addr);
}

/*
 * vma_file_offset - Byte offset in the file that vm_start maps
 * @vma: Pointer to struct vm_area_struct
 *
 * In a 4KB VMA, vm_pgoff still counts 16KB pages, and vm_slice_off is the
 * slice of file page vm_pgoff that vm_start maps. The file offset of
 * vm_start is the start of that page plus the start of the slice. For
 * example, with vm_slice_off = 1:
 *
 *                                vm_start
 *                                    ▼
 *                                    ┌─────┬─────┬─────┐
 *  VMA pages                         │  0  │  1  │  2  │
 *                                    └─────┴─────┴─────┘
 *                              ┌─────┬─────┬─────┬─────┐
 *  slices                      │  0  │  1  │  2  │  3  │
 *              ┌─────┬─────────┼─────┴─────┴─────┴─────┤
 *  file pages  │  0  │   ...   │        vm_pgoff       │
 *              └─────┴─────────┴───────────────────────┘
 *                              ▲     ▲
 *                              │◄───►│ vm_slice_off << PAGE_SHIFT_4KB
 *                              │     └─ vma_file_offset()
 *                              └─ vm_pgoff << PAGE_SHIFT_KERNEL
 *
 * A native VMA has no slice, so this is the usual vm_pgoff << PAGE_SHIFT.
 */
static inline loff_t vma_file_offset(const struct vm_area_struct *vma)
{
	return ((loff_t)vma->vm_pgoff << PAGE_SHIFT_KERNEL) +
	       ((loff_t)vma_slice_off(vma) << PAGE_SHIFT_4KB);
}

/*
 * vma_file_offset_at - Byte offset in the file that @addr maps
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * vm_start maps the file byte at vma_file_offset(), and @addr maps the
 * byte (@addr - vm_start) bytes after it. In a 4KB VMA, vm_start can map
 * any 4KB slice of a 16KB file page.
 *
 * For example, with vm_slice_off = 1, vma_file_offset() is
 * (1 << PAGE_SHIFT_4KB) = 0x1000 bytes into file page vm_pgoff. So
 * @addr = vm_start + 0x4000 maps 0x1000 + 0x4000 = 0x5000 bytes from the
 * start of file page vm_pgoff, which is 0x1000 bytes into file page
 * vm_pgoff + 1:
 *
 *                vm_start                     @addr
 *                    ▼                          ▼
 *                    ┌─────┬─────┬─────┬─────┬─────┐
 *  VMA pages         │  0  │  1  │  2  │  3  │  4  │
 *                    └─────┴─────┴─────┴─────┴─────┘
 *              ┌───────────────────────┬───────────────────────┐
 *  file pages  │        vm_pgoff       │      vm_pgoff + 1     │
 *              └───────────────────────┴───────────────────────┘
 *                    ▲                          ▲
 *                    │◄─── @addr - vm_start ───►│
 *                    │                          └─ vma_file_offset_at()
 *                    └─ vma_file_offset()
 *
 * For a native VMA, vma_file_offset() is the usual vm_pgoff << PAGE_SHIFT.
 */
static inline loff_t vma_file_offset_at(const struct vm_area_struct *vma,
					unsigned long addr)
{
	return vma_file_offset(vma) + (addr - vma->vm_start);
}

/*
 * p3s_filemap_fault_check_eof - Check whether a file fault is past EOF
 * @vma: Pointer to struct vm_area_struct
 * @addr: Faulting virtual address
 * @pgoff: Faulting page offset index
 * @inode: Backing inode pointer
 * @max_idx: Max page index derived from inode size
 *
 * A fault is past EOF if file page @pgoff is at or after @max_idx, the first
 * file page that is entirely past EOF. In a 4KB VMA, a 4KB page in the last
 * file page can also be past EOF: the fault is past EOF if the file offset
 * that @addr maps, vma_file_offset_at(), is at or after i_size.
 *
 * For example, with i_size = 0x9800, @max_idx is
 * DIV_ROUND_UP(0x9800, PAGE_SIZE_KERNEL) = 3, so file page 2, at file
 * offsets 0x8000 to 0xbfff, is the last one. Its slice 1 starts at 0x9000,
 * before i_size, so it can be mapped. Its slices 2 and 3 start at 0xa000
 * and 0xb000, after i_size, so faults on them get SIGBUS:
 *
 *               0x8000    0x9000    0xa000    0xb000    0xc000
 *               ┌─────────┬─────────┬─────────┬─────────┐
 *  file page 2  │ slice 0 │ slice 1 │ slice 2 │ slice 3 │
 *               ├─────────┴─────────┼─────────┴─────────┤
 *  fault        │       mapped      │       SIGBUS      │
 *               └───────────────────┴───────────────────┘
 *                              ▲
 *                              └─ i_size = 0x9800
 */
#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE
#define p3s_filemap_fault_check_eof(vma, addr, pgoff, inode, max_idx) \
	unlikely((pgoff) >= (max_idx) || \
		 (vma_is_p3s_4k(vma) && \
		  vma_file_offset_at((vma), (addr)) >= i_size_read(inode)))
#else
#define p3s_filemap_fault_check_eof(vma, addr, pgoff, inode, max_idx) \
	(((void)(vma), (void)(addr), (void)(inode)), unlikely((pgoff) >= (max_idx)))
#endif


/*
 * vma_linear_page_index - Page offset that @addr maps
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * For a 4KB file VMA, this is the file page that backs @addr. vm_start maps
 * slice vm_slice_off of file page vm_pgoff and each following 4KB page maps
 * the next slice, so count the slices from the start of file page vm_pgoff
 * to @addr and divide by the slices per page.
 *
 * For example, with vm_slice_off = 1 and @addr = vm_start + 0x4000, @addr
 * is (0x4000 >> PAGE_SHIFT_4KB) = 4 pages after vm_start, or 1 + 4 = 5
 * slices after the start of file page vm_pgoff. So it is in file page
 * vm_pgoff + (5 >> P3S_SLICE_SHIFT) = vm_pgoff + 1:
 *
 *                vm_start                     @addr
 *                    ▼                          ▼
 *                    ┌─────┬─────┬─────┬─────┬─────┐
 *  VMA pages         │  0  │  1  │  2  │  3  │  4  │
 *                    └─────┴─────┴─────┴─────┴─────┘
 *              ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 *  slices      │  0  │  1  │  2  │  3  │  0  │  1  │  2  │  3  │
 *              ├─────┴─────┴─────┴─────┼─────┴─────┴─────┴─────┤
 *  file pages  │        vm_pgoff       │      vm_pgoff + 1     │
 *              └───────────────────────┴───────────────────────┘
 *                                               ▲
 *                                               └─ vma_linear_page_index()
 *
 * Anonymous 4KB VMAs count 4KB pages from vm_pgoff instead. Native VMAs,
 * and 4KB VMAs with vm_ops but no file, count 16KB pages.
 */
static inline pgoff_t vma_linear_page_index(const struct vm_area_struct *vma,
					    unsigned long addr)
{
	if (!vma_is_p3s_4k(vma) || (vma->vm_ops && !vma->vm_file))
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
 * vma_native_pages - Number of page offsets that @vma covers
 * @vma: Pointer to struct vm_area_struct
 *
 * @vma covers page offsets vm_pgoff to vm_pgoff + vma_native_pages() - 1.
 * A 4KB file VMA can start and end partway through a 16KB file page, so
 * count the slices from the start of file page vm_pgoff to vm_end and
 * round up to whole file pages.
 *
 * For example, a VMA with vm_slice_off = 2 and vm_end = vm_start + 0x4000
 * is (0x4000 >> PAGE_SHIFT_4KB) = 4 pages long, so it covers
 * DIV_ROUND_UP(2 + 4, P3S_SLICES_PER_PAGE) = 2 file pages although it is
 * only 16KB:
 *
 *                      vm_start                 vm_end
 *                          ▼                       ▼
 *                          ┌─────┬─────┬─────┬─────┐
 *  VMA pages               │  0  │  1  │  2  │  3  │
 *                          └─────┴─────┴─────┴─────┘
 *              ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 *  slices      │  0  │  1  │  2  │  3  │  0  │  1  │  2  │  3  │
 *              ├─────┴─────┴─────┴─────┼─────┴─────┴─────┴─────┤
 *  file pages  │        vm_pgoff       │      vm_pgoff + 1     │
 *              └───────────────────────┴───────────────────────┘
 *              │◄────────── vma_native_pages() = 2 ───────────►│
 *
 * Anonymous 4KB VMAs count 4KB pages, and native VMAs count 16KB pages.
 */
static inline pgoff_t vma_native_pages(const struct vm_area_struct *vma)
{
	unsigned long nr_slices = vma_size(vma) >> mm_pte_shift(vma->vm_mm);

	if (!vma_is_p3s_4k(vma) || !vma->vm_ops)
		return nr_slices;

	return DIV_ROUND_UP(vma_slice_off(vma) + nr_slices,
			    P3S_SLICES_PER_PAGE);
}

/*
 * vma_pgoff_to_address - Address in @vma where page offset @pgoff starts
 * @vma: Pointer to struct vm_area_struct
 * @pgoff: Page offset to look up
 *
 * For a file VMA, returns the address that maps slice 0 of file page
 * @pgoff, clamped to the VMA: vm_start, plus (@pgoff - vm_pgoff) file
 * pages, minus the vm_slice_off slices of file page vm_pgoff that are
 * before vm_start.
 *
 * For example, with vm_slice_off = 1, @pgoff = vm_pgoff + 1 gives vm_start
 * plus one 16KB file page (0x4000) minus one slice (0x1000), which is
 * vm_start + 0x3000, the start of VMA page 3. File page vm_pgoff starts
 * before vm_start, so @pgoff = vm_pgoff gives vm_start:
 *
 *  @pgoff        vm_pgoff        vm_pgoff + 1
 *                    ▼                 ▼
 *                    ┌─────┬─────┬─────┬─────┬─────┐
 *  VMA pages         │  0  │  1  │  2  │  3  │  4  │
 *                    └─────┴─────┴─────┴─────┴─────┘
 *              ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 *  slices      │  0  │  1  │  2  │  3  │  0  │  1  │  2  │  3  │
 *              ├─────┴─────┴─────┴─────┼─────┴─────┴─────┴─────┤
 *  file pages  │        vm_pgoff       │      vm_pgoff + 1     │
 *              └───────────────────────┴───────────────────────┘
 *
 * A page after the VMA gives vm_end. For a VMA without a file, @pgoff
 * counts pages of the process page size.
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
 * vma_folio_slice_bounds - 4KB pages around @addr that map the same 16KB page
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 * @nr_slices: Output number of contiguous slices (1..4)
 * @start_addr: Output starting virtual address of the slice span
 *
 * In a 4KB VMA, finds the 4KB pages around @addr that map slices of the
 * 16KB page backing @addr, without crossing the VMA or the PTE table that
 * maps @addr. Sets @start_addr to the first of them and @nr_slices to
 * their number.
 *
 * For example, with vm_slice_off = 1 and @addr = vm_start + 0x2000, @addr
 * is at slice 1 + (0x2000 >> PAGE_SHIFT_4KB) = 3 of file page vm_pgoff.
 * Slices 1 to 3 of that page are in the VMA, but slice 0 is before
 * vm_start, so @start_addr is vm_start and @nr_slices is 3:
 *
 *                vm_start         @addr
 *                    ▼              ▼
 *                    ┌─────┬─────┬─────┬─────┬─────┐
 *  VMA pages         │  0  │  1  │  2  │  3  │  4  │
 *                    └─────┴─────┴─────┴─────┴─────┘
 *                    │◄───────────────►│ @nr_slices = 3
 *              ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 *  slices      │  0  │  1  │  2  │  3  │  0  │  1  │  2  │  3  │
 *              ├─────┴─────┴─────┴─────┼─────┴─────┴─────┴─────┤
 *  file pages  │        vm_pgoff       │      vm_pgoff + 1     │
 *              └───────────────────────┴───────────────────────┘
 *
 * For a native VMA, @nr_slices is 1 and @start_addr is @addr.
 */
static inline void vma_folio_slice_bounds(const struct vm_area_struct *vma,
					  unsigned long addr,
					  unsigned int *nr_slices,
					  unsigned long *start_addr)
{
	unsigned int slices_per_folio = P3S_SLICES_PER_PAGE;
	unsigned int sub, left, right;
	unsigned long vma_before, vma_after, pt_before, pt_after;

	if (!vma_is_p3s_4k(vma)) {
		*nr_slices = 1;
		*start_addr = addr;
		return;
	}

	addr &= PAGE_MASK_4KB;
	sub = vma_slice_offset(vma, addr);
	vma_before = (addr >= vma->vm_start) ?
		((addr - vma->vm_start) >> PAGE_SHIFT_4KB) : 0;
	vma_after = (vma->vm_end > addr) ?
		((vma->vm_end - addr) >> PAGE_SHIFT_4KB) : 0;
	pt_before = (addr >> PAGE_SHIFT_4KB) & 511;
	pt_after = 512 - pt_before;

	left = min3((unsigned long)sub, vma_before, pt_before);
	right = min3((unsigned long)(slices_per_folio - sub), vma_after, pt_after);

	*nr_slices = left + right;
	*start_addr = addr - ((unsigned long)left << PAGE_SHIFT_4KB);
}

/*
 * VMA Merge Struct Geometry Abstractions (vmg)
 */
#define vmg_is_p3s_4k(vmg)	mm_is_p3s_4k((vmg)->mm)

/*
 * vma_can_merge_offsets - Check that the range at @boundary continues @left
 * @left: Left (lower-address) VMA
 * @right_pgoff: Page offset of right (higher-address) range
 * @right_slice: Starting subpage slice of right range (0..3)
 * @boundary: Virtual address where left ends and right begins
 * @is_p3s_4k: Whether process is running in 4KB compat mode
 * @is_file: Whether mapping is file-backed
 *
 * The range at @boundary can merge with @left only if it starts with what
 * @left would map at @boundary: page offset vma_linear_page_index() and,
 * for a 4KB file VMA, slice vma_slice_offset() of @left at @boundary.
 *
 * For example, let @left have vm_slice_off = 1 and let @boundary be
 * left->vm_start + 0x4000, (0x4000 >> PAGE_SHIFT_4KB) = 4 pages after
 * left->vm_start, or 1 + 4 = 5 slices after the start of file page
 * vm_pgoff. Then the right range must start at slice 5 & P3S_SLICE_MASK = 1
 * of file page vm_pgoff + (5 >> P3S_SLICE_SHIFT) = vm_pgoff + 1:
 *
 *             left->vm_start             @boundary
 *                    ▼                       ▼
 *                    ┌───────────────────────┬─────────────────┐
 *  VMAs              │         @left         │   right range   │
 *                    └───────────────────────┴─────────────────┘
 *              ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
 *  slices      │  0  │  1  │  2  │  3  │  0  │  1  │  2  │  3  │
 *              ├─────┴─────┴─────┴─────┼─────┴─────┴─────┴─────┤
 *  file pages  │        vm_pgoff       │      vm_pgoff + 1     │
 *              └───────────────────────┴───────────────────────┘
 *
 * Native and anonymous VMAs only need matching page offsets.
 */
static inline bool vma_can_merge_offsets(const struct vm_area_struct *left,
					 pgoff_t right_pgoff,
					 unsigned short right_slice,
					 unsigned long boundary,
					 bool is_p3s_4k, bool is_file)
{
	return left && vma_linear_page_index(left, boundary) == right_pgoff &&
	       (!is_p3s_4k || !is_file ||
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
 * p3s_adjust_unmapped_area_info - Align unmapped area allocations to host page
 * @info: Pointer to struct vm_unmapped_area_info
 *
 * In a 4KB compatibility process, virtual memory is backed by 16KB host
 * folios. When searching for an unmapped address range (mmap), aligning the
 * allocation to 16KB host page boundaries (~PAGE_MASK_KERNEL) ensures new
 * VMAs naturally start at slice 0 (folio boundary) unless the caller
 * requested an explicit alignment. This prevents initial subpage slice offsets
 * (vm_slice_off), avoids wasting the host folio at the start, and minimizes
 * fragmentation.
 */
#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE
#define p3s_adjust_unmapped_area_info(info) do { \
	if (!(info)->align_mask && current->mm && mm_is_p3s_4k(current->mm)) \
		(info)->align_mask = ~PAGE_MASK_KERNEL; \
} while (0)
#else
#define p3s_adjust_unmapped_area_info(info) ((void)(info))
#endif

/*
 * vma_folio_slices_exclusive - Small anon folio mapped only by this process
 * @vma: Pointer to struct vm_area_struct mapping @folio
 * @folio: Folio found through a PTE of @vma
 *
 * In a 4KB compatibility process a small anonymous folio can be mapped by
 * up to four 4KB slice PTEs, so its mapcount can exceed its page count while
 * every mapping still belongs to this process (PageAnonExclusive). A macro
 * because the folio flag helpers are not yet defined where this header is
 * included from <linux/mm_types.h>.
 */
#define vma_folio_slices_exclusive(vma, folio)				\
	(vma_is_p3s_4k(vma) && folio_test_anon(folio) &&		\
	 !folio_test_large(folio) && PageAnonExclusive(&(folio)->page))

#endif /* _LINUX_P3S_VMA_H */
