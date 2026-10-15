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

#endif /* __MM_P3S_H */
