/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_PGALLOC_H
#define _LKL_PGALLOC_H

#include <linux/mm.h>
#include <linux/mmzone.h>

#include <asm-generic/pgalloc.h> /* for pte_{alloc,free}_one */

#ifndef CONFIG_MMU
/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t pte)
{
	// pmd_set(pmd, (pte_t *)(page_to_pa(pte) + PAGE_OFFSET));
}
#define pmd_pgtable(pmd) pmd_page(pmd)

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	// pmd_set(pmd, pte);
}

extern pgd_t *pgd_alloc(struct mm_struct *mm);

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	// free_page((unsigned long)pgd);
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	//pmd_t *ret = (pmd_t *)__get_free_page(GFP_PGTABLE_USER);
	return NULL; //ret;
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	// free_page((unsigned long)pmd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	return NULL;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
}

static inline void pte_free(struct mm_struct *mm, pgtable_t ptep)
{
}

#define __pte_free_tlb(tlb, pte, addr) pte_free((tlb)->mm, pte)

#define check_pgt_cache()                                                      \
	do {                                                                   \
	} while (0)

static inline p4d_t *p4d_alloc(struct mm_struct *mm, pgd_t *pgd,
		unsigned long address)
{
	return NULL;
}

static inline pud_t *pud_alloc(struct mm_struct *mm, p4d_t *p4d,
		unsigned long address)
{
	return NULL;
}

static inline pmd_t *pmd_alloc(struct mm_struct *mm, pud_t *pud, unsigned long address)
{
	return NULL;
}
#else // CONFIG_MMU
#define pmd_populate_kernel(mm, pmd, pte) \
	set_pmd(pmd, __pmd(_PAGE_TABLE + (unsigned long) __pa(pte)))

#define pmd_populate(mm, pmd, pte) 				\
	set_pmd(pmd, __pmd(_PAGE_TABLE +			\
		((unsigned long long)page_to_pfn(pte) <<	\
			(unsigned long long) PAGE_SHIFT)))
#define pmd_pgtable(pmd) pmd_page(pmd)

/*
 * Allocate and free page tables.
 */
extern pgd_t *pgd_alloc(struct mm_struct *);

#define __pte_free_tlb(tlb,pte, address)		\
do {							\
	pgtable_pte_page_dtor(pte);			\
	tlb_remove_page((tlb),(pte));			\
} while (0)
#endif // CONFIG_MMU

#endif /* _LKL_PGALLOC_H */
