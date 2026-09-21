/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_P3S_H
#define __MM_P3S_H

#include <linux/p3s/vma.h>
#include <linux/mm.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/userfaultfd_k.h>
#include <linux/huge_mm.h>
#include <linux/oom.h>
#include <linux/pgtable.h>
#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE
#include <asm/p3s/mte.h>
#endif

/*
 * vma_pte_add_slice - Apply subpage slice offset to a PTE value
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 * @pte: Base page table entry
 *
 * In 4KB compatibility mode, encodes the subpage slice index (0..3) into
 * the PTE physical frame bits.
 */
static inline pte_t vma_pte_add_slice(const struct vm_area_struct *vma,
				      unsigned long addr,
				      pte_t pte)
{
	if (vma_is_p3s_4k(vma)) {
		unsigned int slice = vma_slice_offset(vma, addr);

		return __pte(pte_val(pte) + ((pteval_t)slice << PAGE_SHIFT_4KB));
	}
	return pte;
}

/*
 * vma_folio_mk_pte - Construct PTE for a folio mapped at virtual address
 * @vma: Pointer to struct vm_area_struct
 * @folio: Host physical folio
 * @addr: Virtual address within @vma
 *
 * Computes the correct physical frame number across large folio boundaries
 * and applies subpage slice offsets for 4KB compat mode.
 */
static inline pte_t vma_folio_mk_pte(const struct vm_area_struct *vma,
				     const struct folio *folio,
				     unsigned long addr)
{
	unsigned long pfn = folio_pfn(folio);

	if (vma->vm_file && folio_test_large(folio)) {
		pgoff_t pgoff = vma_linear_page_index(vma, addr);

		if (pgoff > folio->index)
			pfn += pgoff - folio->index;
	}

	return vma_pte_add_slice(vma, addr, pfn_pte(pfn, vma->vm_page_prot));
}

/*
 * vma_remap_frame - First PTE frame for remap_pfn_range() at @addr
 * @vma: VMA being remapped
 * @addr: First virtual address being remapped
 * @pfn: Kernel page frame number to map at @addr
 *
 * remap_pfn_range() maps consecutive frames, one per PTE. The PTEs of a
 * 4KB compatibility VMA map 4KB frames: start at the slice of @pfn that
 * vma_slice_offset() assigns to @addr, as insert_pfn() does.
 */
static inline unsigned long vma_remap_frame(const struct vm_area_struct *vma,
					    unsigned long addr,
					    unsigned long pfn)
{
	if (!vma_is_p3s_4k(vma))
		return pfn;
	return (pfn << P3S_SLICE_SHIFT) + vma_slice_offset(vma, addr);
}

/*
 * mm_frame_pfn - Kernel page frame number of PTE frame @frame of @mm
 * @mm: Pointer to struct mm_struct
 * @frame: Frame number in units of the page size of @mm
 */
static inline unsigned long mm_frame_pfn(const struct mm_struct *mm,
					 unsigned long frame)
{
	return mm_is_p3s_4k(mm) ? frame >> P3S_SLICE_SHIFT : frame;
}

/*
 * mm_frame_pte - Construct a PTE of @mm that maps PTE frame @frame
 * @mm: Pointer to struct mm_struct
 * @frame: Frame number in units of the page size of @mm
 * @prot: Page protection
 */
static inline pte_t mm_frame_pte(const struct mm_struct *mm,
				 unsigned long frame, pgprot_t prot)
{
	pte_t pte = pfn_pte(mm_frame_pfn(mm, frame), prot);

	if (!mm_is_p3s_4k(mm))
		return pte;
	return __pte(pte_val(pte) +
		     ((pteval_t)(frame & P3S_SLICE_MASK) << PAGE_SHIFT_4KB));
}

/*
 * vma_pfnmap_phys - Physical address of the frame a PFN mapping maps
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 * @pfn: Kernel page frame number found by follow_pfnmap_start()
 *
 * Adds the slice that a 4KB compatibility VMA maps at @addr, as set up by
 * remap_pfn_range() and insert_pfn().
 */
static inline phys_addr_t vma_pfnmap_phys(const struct vm_area_struct *vma,
					  unsigned long addr,
					  unsigned long pfn)
{
	return ((phys_addr_t)pfn << PAGE_SHIFT_KERNEL) +
	       ((phys_addr_t)vma_slice_offset(vma, addr) << PAGE_SHIFT_4KB);
}

/*
 * p3s_pte_maps_folio - Check if a PTE maps the given normal folio at addr
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address corresponding to @ptent
 * @ptent: Page table entry value
 * @folio: Expected folio pointer
 */
static inline bool p3s_pte_maps_folio(struct vm_area_struct *vma,
				      unsigned long addr, pte_t ptent,
				      const struct folio *folio)
{
	return pte_present(ptent) &&
	       vm_normal_folio(vma, addr, ptent) == folio;
}

/*
 * p3s_count_folio_slices - Count how many PTEs in a slice window map @folio
 * @vma: Pointer to struct vm_area_struct
 * @pte_base: Starting PTE pointer of the slice window
 * @start_addr: Starting virtual address of the slice window
 * @nr_slices: Number of slices in the window
 * @folio: Target folio to count
 */
static inline unsigned int p3s_count_folio_slices(struct vm_area_struct *vma,
						  pte_t *pte_base,
						  unsigned long start_addr,
						  unsigned int nr_slices,
						  const struct folio *folio)
{
	unsigned int i, count = 0;

	for (i = 0; i < nr_slices; i++) {
		if (p3s_pte_maps_folio(vma, start_addr + (i << PAGE_SHIFT_4KB),
				       ptep_get(pte_base + i), folio))
			count++;
	}
	return count;
}

/*
 * vma_slice_walk_start - First address of the slice window at @addr
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * A 4KB compatibility task maps the slices of one host page with up to
 * four consecutive 4KB PTEs, the PTE at @addr mapping slice
 * vma_slice_offset(). Returns the first address of that window within
 * @vma, where an rmap walk for a small folio must start, or @addr for
 * other VMAs.
 */
static inline unsigned long vma_slice_walk_start(const struct vm_area_struct *vma,
						 unsigned long addr)
{
	unsigned long off;

	if (!vma_is_p3s_4k(vma))
		return addr;

	off = (unsigned long)vma_slice_offset(vma, addr) << PAGE_SHIFT_4KB;
	addr &= PAGE_MASK_4KB;
	return addr - min(off, addr - vma->vm_start);
}

/*
 * vma_slice_walk_end - End of an rmap walk for a small folio from @addr
 * @vma: Pointer to struct vm_area_struct
 * @addr: Virtual address within @vma
 *
 * For 4KB compatibility VMAs, the end of the slice window at @addr within
 * @vma, so that the walk visits every slice PTE that can map the folio.
 * One page past @addr for other VMAs.
 */
static inline unsigned long vma_slice_walk_end(const struct vm_area_struct *vma,
					       unsigned long addr)
{
	unsigned long off;

	if (!vma_is_p3s_4k(vma))
		return addr + vma_page_size(vma);

	off = (unsigned long)vma_slice_offset(vma, addr) << PAGE_SHIFT_4KB;
	addr &= PAGE_MASK_4KB;
	return addr + min(PAGE_SIZE_KERNEL - off, vma->vm_end - addr);
}

/*
 * p3s_folio_slices_in_window - Check that all mappings of @folio are here
 * @vma: Pointer to struct vm_area_struct
 * @folio: Folio about to be unmapped by reclaim or migration
 * @addr: Address of a PTE in @vma that maps @folio
 * @pte: That PTE, mapped and locked
 *
 * A 4KB compatibility task can map a small anonymous folio with several
 * slice PTEs, all covered by its one PageAnonExclusive flag. Unmapping the
 * first of them clears the flag (folio_try_share_anon_rmap_pte()), so the
 * other slices must be unmapped in the same walk: a slice left mapped
 * writable would reference a page that is no longer exclusive. Returns
 * true if every mapping of @folio is a PTE in the slice window at @addr,
 * which the walk covers under the page table lock it already holds.
 */
static inline bool p3s_folio_slices_in_window(struct vm_area_struct *vma,
					      struct folio *folio,
					      unsigned long addr, pte_t *pte)
{
	unsigned int nr_slices;
	unsigned long start;

	if (!vma_is_p3s_4k(vma) || folio_test_large(folio) ||
	    folio_mapcount(folio) == 1)
		return true;

	vma_folio_slice_bounds(vma, addr, &nr_slices, &start);
	return p3s_count_folio_slices(vma,
				      pte - ((addr - start) >> PAGE_SHIFT_4KB),
				      start, nr_slices, folio) ==
	       folio_mapcount(folio);
}

/*
 * p3s_folio_mapped_by_slices - Check if other slice PTEs still map @folio
 * @vma: Pointer to struct vm_area_struct
 * @folio: Folio whose PTE in @vma is being unmapped
 *
 * When reclaim unmaps a small folio that a 4KB compatibility task maps
 * with several slice PTEs, each of them is replaced by a swap PTE for the
 * same swap entry. Only a swap PTE that replaces the folio's last mapping
 * may be marked exclusive: do_swap_page() makes the folio PageAnonExclusive
 * when it maps an exclusive entry, and must not find it exclusive while
 * another swap PTE still refers to it.
 */
static inline bool p3s_folio_mapped_by_slices(const struct vm_area_struct *vma,
					      const struct folio *folio)
{
	return vma_is_p3s_4k(vma) && !folio_test_large(folio) &&
	       folio_mapcount(folio) > 1;
}

/*
 * vma_anon_index_matches - Check an anonymous page's index against @addr
 * @vma: Pointer to struct vm_area_struct
 * @pgoff: Index of the page (page_pgoff())
 * @addr: Virtual address the page is mapped at
 *
 * In a 4KB compatibility anonymous VMA, the index of a small folio is that
 * of the slice that faulted first, and its other slices are mapped in the
 * same 16KB window, so compare the windows. Elsewhere the index must match
 * linear_page_index() exactly.
 */
static inline bool vma_anon_index_matches(const struct vm_area_struct *vma,
					  pgoff_t pgoff, unsigned long addr)
{
	unsigned long first;

	if (!vma_is_p3s_4k(vma) || vma->vm_ops)
		return pgoff == vma_linear_page_index(vma, addr);

	first = vma->vm_start + ((pgoff - vma->vm_pgoff) << PAGE_SHIFT_4KB);
	return (first & PAGE_MASK_KERNEL) == (addr & PAGE_MASK_KERNEL);
}

#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE

/*
 * p3s_anon_folio_lookaround_pte - Find adjacent anonymous folio in slice window
 * @vma: Pointer to struct vm_area_struct
 * @pte: Faulting PTE pointer
 * @addr: Faulting virtual address
 *
 * Returns a referenced sibling folio whose slice for @addr is unused, or
 * NULL. A folio qualifies only if every mapping of it is a sibling PTE in
 * this window: a slice moved elsewhere (for example by mremap()), still
 * referenced by a swap entry (folio in the swap cache), or replaced by a
 * migration entry is in use, and mapping it again would alias it. The
 * folio lock excludes in-flight migration and reclaim.
 */
static inline struct folio *p3s_anon_folio_lookaround_pte(struct vm_area_struct *vma,
							  pte_t *pte,
							  unsigned long addr)
{
	unsigned int total_slices;
	unsigned long window_start;
	pte_t *pte_base;
	unsigned int i;

	if (!vma_is_p3s_4k(vma))
		return NULL;

	vma_folio_slice_bounds(vma, addr, &total_slices, &window_start);
	if (total_slices <= 1)
		return NULL;

	pte_base = pte - ((addr - window_start) >> PAGE_SHIFT_4KB);

	for (i = 0; i < total_slices; i++) {
		unsigned long slice_addr = window_start + (i << PAGE_SHIFT_4KB);
		pte_t ptent = ptep_get(pte_base + i);
		struct folio *f;
		bool ok;

		if (!pte_present(ptent))
			continue;

		f = vm_normal_folio(vma, slice_addr, ptent);
		if (!f || !folio_test_anon(f) || folio_test_large(f) ||
		    folio_test_ksm(f))
			continue;

		if (!PageAnonExclusive(&f->page))
			continue;

		if (!folio_trylock(f))
			continue;
		ok = !folio_test_swapcache(f) &&
		     p3s_count_folio_slices(vma, pte_base, window_start,
					    total_slices, f) == folio_mapcount(f) &&
		     folio_try_get(f);
		folio_unlock(f);
		if (ok)
			return f;
	}

	return NULL;
}

/*
 * p3s_anon_folio_reuse - Update accounting when reusing an existing folio
 * @folio: Anonymous folio being reused
 * @vma: Pointer to struct vm_area_struct
 *
 * The folio reference taken by p3s_anon_folio_lookaround_pte() is
 * transferred to the new slice mapping.
 */
static inline void p3s_anon_folio_reuse(struct folio *folio,
					struct vm_area_struct *vma)
{
	atomic_inc(&folio->_mapcount);
	inc_mm_counter(vma->vm_mm, MM_ANONPAGES);
}

#ifdef CONFIG_USERFAULTFD
/*
 * p3s_uffd_install_anon_folio - Look around and reuse existing folio slice for userfaultfd
 * @dst_vma: Destination VMA
 * @dst_pte: Destination PTE pointer
 * @dst_addr: Destination virtual address
 * @folio: Newly allocated source folio
 * @dst_pte_val: Output PTE value to install
 * @writable: Whether the mapping should be writable
 * @flags: Userfaultfd atomic flags
 *
 * Returns true if an existing slice was reused and folio_put was called on @folio.
 */
static inline bool p3s_uffd_install_anon_folio(struct vm_area_struct *dst_vma,
					       pte_t *dst_pte,
					       unsigned long dst_addr,
					       struct folio *folio,
					       pte_t *dst_pte_val,
					       bool writable,
					       uffd_flags_t flags)
{
	struct folio *existing_folio =
		p3s_anon_folio_lookaround_pte(dst_vma, dst_pte, dst_addr);
	unsigned long offset;

	if (!existing_folio)
		return false;

	offset = (unsigned long)vma_slice_offset(dst_vma, dst_addr) << PAGE_SHIFT_4KB;

	/* The data is replaced below; the old allocation tags must go too */
	if (dst_vma->vm_flags & VM_MTE)
		mte_zero_clear_slice_tags(folio_address(existing_folio) + offset);
	memcpy(folio_address(existing_folio) + offset,
	       folio_address(folio) + offset, PAGE_SIZE_4KB);
	flush_dcache_folio(existing_folio);

	p3s_anon_folio_reuse(existing_folio, dst_vma);
	*dst_pte_val = vma_folio_mk_pte(dst_vma, existing_folio, dst_addr);
	*dst_pte_val = pte_mkdirty(*dst_pte_val);
	if (writable)
		*dst_pte_val = pte_mkwrite(*dst_pte_val, dst_vma);
	if (flags & MFILL_ATOMIC_WP)
		*dst_pte_val = pte_mkuffd_wp(*dst_pte_val);

	folio_put(folio);
	return true;
}
#endif /* CONFIG_USERFAULTFD */

#else /* !CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

#ifdef CONFIG_USERFAULTFD
static inline bool p3s_uffd_install_anon_folio(struct vm_area_struct *dst_vma,
					       pte_t *dst_pte,
					       unsigned long dst_addr,
					       struct folio *folio,
					       pte_t *dst_pte_val,
					       bool writable,
					       uffd_flags_t flags)
{
	return false;
}
#endif

#endif /* CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

#endif /* __MM_P3S_H */
