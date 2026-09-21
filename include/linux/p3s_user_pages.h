/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_USER_PAGES_H
#define _LINUX_P3S_USER_PAGES_H

#include <linux/p3s.h>
#ifdef CONFIG_ARM64
#include <asm/p3s.h>
#include <asm/p3s/mmu.h>
#endif

#if defined(CONFIG_ARM64_PER_PROCESS_PAGE_SIZE) && !defined(__ASSEMBLY__)

/* Redefining PAGE_SHIFT and PAGE_SIZE automatically makes PAGE_SHIFT-derived macros dynamic. */
#undef PAGE_SHIFT
#undef PAGE_SIZE
#undef PTRS_PER_PGD
#undef TASK_SIZE
#undef STACK_TOP
#undef DEFAULT_MAP_WINDOW
#undef TASK_UNMAPPED_BASE
#undef stack_guard_gap
#undef STACK_TOP_MAX

#define PAGE_SHIFT		mm_pte_shift(p3s_current_mm())
#define PAGE_SIZE		(1UL << PAGE_SHIFT)
#define PTRS_PER_PGD		PTRS_PER_PTE
#define TASK_SIZE		mm_task_size(p3s_current_mm())
#define STACK_TOP		mm_stack_top(p3s_current_mm())
#define DEFAULT_MAP_WINDOW	mm_default_map_window(p3s_current_mm())
#define TASK_UNMAPPED_BASE	mm_task_unmapped_base(p3s_current_mm())
#define stack_guard_gap		mm_stack_guard_gap(p3s_current_mm())
#define STACK_TOP_MAX		mm_stack_top_max(p3s_current_mm())

#undef PFN_PHYS
#undef PHYS_PFN
#define PFN_PHYS(x)		((phys_addr_t)(x) << PAGE_SHIFT_KERNEL)
#define PHYS_PFN(x)		((unsigned long)((x) >> PAGE_SHIFT_KERNEL))

#undef pte_index
#define pte_index(addr)		(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))

#undef pmd_index
#define pmd_index(addr)		(((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))

#undef pud_index
#define pud_index(addr)		(((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1))

#undef pmd_offset
#define pmd_offset(pud, addr)	(pud_pgtable(*(pud)) + pmd_index(addr))

#undef pgd_offset
#define pgd_offset(mm, addr)	({ p3s_assert_mm(mm); mm_pgd_offset((mm), (addr)); })

#define vma_is_compat(vma)	({ p3s_assert_mm((vma)->vm_mm); vma_is_compat(vma); })
#define vma_pages(vma)		({ p3s_assert_mm((vma)->vm_mm); vma_pages(vma); })

#endif /* CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

#endif /* _LINUX_P3S_USER_PAGES_H */
