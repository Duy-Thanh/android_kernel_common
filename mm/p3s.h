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
#include <linux/pgtable.h>

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
	if (vma_is_compat(vma)) {
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
 * p3s_pte_range_none - Check if a contiguous span of PTEs are all unmapped (none)
 * @pte: Starting PTE pointer
 * @nr_pages: Number of entries to inspect
 */
static inline bool p3s_pte_range_none(pte_t *pte, int nr_pages)
{
	int i;

	for (i = 0; i < nr_pages; i++) {
		if (!pte_none(ptep_get_lockless(pte + i)))
			return false;
	}

	return true;
}

/*
 * vma_folio_clamp_none_ptes - Clamp batch to contiguous empty PTEs
 * @vma: Pointer to struct vm_area_struct
 * @vmf: Fault information
 * @addr: In/Out starting virtual address of the slice batch
 * @nr_pages: In/Out number of pages in the slice batch
 *
 * In compatibility mode (e.g. 4KB process on 16KB host), an anonymous page
 * fault optimistically prepares a multi-page batch spanning up to an entire
 * host folio (P3S_SLICES_PER_PAGE = 4 slices). When subpage slices within
 * the same 16KB boundary have already been mapped by earlier faults (e.g.,
 * stack expansion during execve argument setup), the initial batch may
 * overlap existing PTEs.
 *
 * Under the page table lock, this helper clamps the batch to the contiguous
 * sub-range of empty (pte_none) PTEs that contains the faulting address
 * (@vmf->address).
 *
 * ┌──────┬──────┬──────┬──────┐
 * │ S0   │ S1   │ S2   │ S3   │
 * │ none │ none │ none │ mapped
 * └──────┴──────┴──────┴──────┘
 * ▲             ▲      ▲
 * └── *addr     │      └── pre-existing PTE
 *               └── vmf->address (fault)
 * ◄── clamped batch (nr = 3) ─►
 *
 * If the faulting PTE itself is already mapped (e.g. concurrent fault),
 * the batch is collapsed to nr_pages = 1 at vmf->address so standard
 * change detection triggers.
 */
static inline void vma_folio_clamp_none_ptes(const struct vm_area_struct *vma,
					     struct vm_fault *vmf,
					     unsigned long *addr,
					     int *nr_pages)
{
	unsigned int offset;
	int lo, hi;

	if (*nr_pages <= 1 || p3s_pte_range_none(vmf->pte, *nr_pages))
		return;

	offset = (vmf->address - *addr) >> mm_pte_shift(vma->vm_mm);
	if (!pte_none(ptep_get(vmf->pte + offset))) {
		vmf->pte += offset;
		*addr = vmf->address;
		*nr_pages = 1;
		return;
	}

	lo = offset;
	hi = offset + 1;

	while (lo > 0 && pte_none(ptep_get(vmf->pte + lo - 1)))
		lo--;
	while (hi < *nr_pages && pte_none(ptep_get(vmf->pte + hi)))
		hi++;

	vmf->pte += lo;
	*addr += (unsigned long)lo << mm_pte_shift(vma->vm_mm);
	*nr_pages = hi - lo;
}

#endif /* __MM_P3S_H */
