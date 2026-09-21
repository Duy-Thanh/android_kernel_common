/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_P3S_PGTABLE_H
#define _ASM_ARM64_P3S_PGTABLE_H

#include <linux/p3s/mm.h>
#include <asm/pgtable-hwdef.h>
#include <asm/memory.h>

/*
 * ARM64 Unified 3-Level Translation Geometry:
 *
 * 16KB Native (47-bit VA):
 *   PGD (L1, 11b) ---> PMD (L2, 11b) ---> PTE (L3, 11b) ---> Offset (14b)
 *
 * 4KB Compat (39-bit VA):
 *   PGD (L1,  9b) ---> PMD (L2,  9b) ---> PTE (L3,  9b) ---> Offset (12b)
 */

static inline unsigned int mm_ptdesc_table_shift(const struct mm_struct *mm)
{
	return mm_pte_shift(mm) - PTDESC_ORDER;
}

static inline unsigned long mm_ptrs_per_pte(const struct mm_struct *mm)
{
	return _AC(1, UL) << mm_ptdesc_table_shift(mm);
}

static inline unsigned int mm_pmd_shift(const struct mm_struct *mm)
{
	return mm_pte_shift(mm) + mm_ptdesc_table_shift(mm);
}

static inline unsigned long mm_ptrs_per_pmd(const struct mm_struct *mm)
{
	return mm_ptrs_per_pte(mm);
}

static inline unsigned int mm_pgdir_shift(const struct mm_struct *mm)
{
	return mm_pmd_shift(mm) + mm_ptdesc_table_shift(mm);
}

static inline unsigned long mm_ptrs_per_pgd(const struct mm_struct *mm)
{
	return mm_ptrs_per_pte(mm);
}

static inline unsigned long mm_pgd_index(const struct mm_struct *mm,
					unsigned long addr)
{
	return (addr >> mm_pgdir_shift(mm)) & (mm_ptrs_per_pgd(mm) - 1);
}

static inline unsigned long mm_pmd_index(const struct mm_struct *mm,
					unsigned long addr)
{
	return (addr >> mm_pmd_shift(mm)) & (mm_ptrs_per_pmd(mm) - 1);
}

static inline unsigned long mm_pte_index(const struct mm_struct *mm,
					unsigned long addr)
{
	return (addr >> mm_pte_shift(mm)) & (mm_ptrs_per_pte(mm) - 1);
}

static inline pgd_t *mm_pgd_offset(const struct mm_struct *mm,
				   unsigned long addr)
{
	return mm->pgd + mm_pgd_index(mm, addr);
}
/*
 * In opt-in translation units, override standard pgd_offset() to use the
 * per-MM PGD indexing helper.
 */
#define pgd_offset(mm, addr) mm_pgd_offset((mm), (addr))

static inline pmd_t *mm_pmd_offset(const struct mm_struct *mm,
				   const pud_t *pud, unsigned long addr)
{
	return (pmd_t *)__va(pud_val(*pud) & PHYS_MASK & (s32)PAGE_MASK) +
	       mm_pmd_index(mm, addr);
}

static inline pte_t *mm_pte_offset_kernel(const struct mm_struct *mm,
					  const pmd_t *pmd, unsigned long addr)
{
	return (pte_t *)__va(pmd_val(*pmd) & PHYS_MASK & (s32)PAGE_MASK) +
	       mm_pte_index(mm, addr);
}

/*
 * contpte_is_enabled - Check if ContPTE contiguous PTEs are supported
 * @mm: Pointer to struct mm_struct
 *
 * ContPTE folding requires uniform native hardware translation granules and is
 * disabled for 4KB P3S compatibility address spaces.
 */
static inline bool contpte_is_enabled(const struct mm_struct *mm)
{
	return !mm_is_p3s_4k(mm);
}

/*
 * pte_advance_phys - Advance the physical address of a PTE by @bytes
 * @pte: Base page table entry
 * @bytes: Byte offset to advance the physical address
 *
 * In 4KB compat mode on a 16KB kernel, subpage slice operations (such as
 * anonymous folio subpage reuse and filemap slice mapping) advance PTEs to
 * subsequent subpage slices within a folio.
 *
 * Arm64 PTEs encode both physical address bits and hardware descriptor flags
 * (permissions, memory attributes, shareability, dirty/young bits). To
 * advance the physical address without corrupting or losing any architecture
 * flags:
 *   1. Extract the physical address via __pte_to_phys().
 *   2. Isolate the pure protection/attribute flags by XORing the raw PTE value
 *      with its encoded physical address bits (pte_val ^ __phys_to_pte_val).
 *   3. Advance the physical address by @bytes and re-combine with the preserved
 *      protection flags via bitwise-OR.
 */
#define pte_advance_phys(pte, bytes) ({					\
	pte_t __pte_adv = (pte);					\
	phys_addr_t __phys_adv = __pte_to_phys(__pte_adv);		\
	pteval_t __prot_adv = pte_val(__pte_adv) ^			\
			      __phys_to_pte_val(__phys_adv);		\
	__pte(__phys_to_pte_val(__phys_adv + (bytes)) | __prot_adv);	\
})

/*
 * When 3-level page tables are active (4KB 39-bit VA or 16KB 47-bit VA), PUD
 * is folded into PGD. In that configuration, PUD-level page table checks are
 * omitted because PUDs are not allocated as distinct tables.
 */
#ifndef __PAGETABLE_PMD_FOLDED
#define __p3s_ptc_pud_set(mm, ptep, pte, nr) \
	page_table_check_puds_set(mm, (pud_t *)(ptep), pte_pud(pte), nr)
#else
#define __p3s_ptc_pud_set(mm, ptep, pte, nr) \
	do { (void)(mm); (void)(ptep); (void)(pte); (void)(nr); } while (0)
#endif

/*
 * p3s_page_table_check_ptes_set - Polymorphic CONFIG_PAGE_TABLE_CHECK hook
 * @mm: Pointer to struct mm_struct
 * @ptep: Pointer to the page table entry being updated
 * @pte: The new entry value
 * @nr: Number of consecutive entries being written
 * @pgsize: Page size of the mapping being established
 *
 * In a mixed-granule system, page table entries can represent either 16KB host
 * geometry (16KB PTE, 32MB PMD) or 4KB compat geometry (4KB PTE, 2MB PMD).
 *
 * Dispatches to page_table_check_{ptes,pmds,puds}_set() based on the mapping
 * size @pgsize, ensuring entries are tracked and validated at the appropriate
 * page table level under both native and compat address spaces.
 */
#define p3s_page_table_check_ptes_set(mm, ptep, pte, nr, pgsize) do {		\
	unsigned long __pgsz = (pgsize);					\
	if (__pgsz == PAGE_SIZE_KERNEL || __pgsz == PAGE_SIZE_4KB)		\
		page_table_check_ptes_set(mm, ptep, pte, nr);			\
	else if (__pgsz == PMD_SIZE || __pgsz == PMD_SIZE_4KB)			\
		page_table_check_pmds_set(mm, (pmd_t *)(ptep), pte_pmd(pte), nr);\
	else if (__pgsz == PUD_SIZE || __pgsz == PUD_SIZE_4KB)			\
		__p3s_ptc_pud_set(mm, ptep, pte, nr);				\
	else									\
		VM_WARN_ON(1);							\
} while (0)

#endif /* _ASM_ARM64_P3S_PGTABLE_H */
