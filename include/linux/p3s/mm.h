/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_MM_H
#define _LINUX_P3S_MM_H

#include <linux/kernel.h>
#include <linux/personality.h>
#include <linux/p3s/const.h>

struct mm_struct;
struct task_struct;

#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE

enum p3s_mode {
	P3S_MODE_OFF = 0,
	P3S_MODE_ON = 1,
	P3S_MODE_ALTERNATE = 2,
};

extern enum p3s_mode p3s_mode;

static inline bool personality_4kb_pages(unsigned long pers)
{
	return !!(pers & ADDR_PAGE_SIZE_4KB);
}

static inline unsigned int mm_pte_shift(const struct mm_struct *mm)
{
	return (mm && mm->pte_shift) ? mm->pte_shift : PAGE_SHIFT_KERNEL;
}

static inline void mm_set_pte_shift(struct mm_struct *mm, unsigned int shift)
{
	if (mm)
		mm->pte_shift = shift;
}

static inline void mm_init_pte_shift(struct mm_struct *mm,
				     const struct task_struct *p)
{
	bool use_4kb = system_state >= SYSTEM_RUNNING &&
		       (personality_4kb_pages(current->personality) ||
			(p && personality_4kb_pages(p->personality)) ||
			p3s_mode == P3S_MODE_ON ||
			(p3s_mode == P3S_MODE_ALTERNATE &&
			 ((p ? p->pid : current->pid) & 1)));

	mm_set_pte_shift(mm, use_4kb ? PAGE_SHIFT_4KB : PAGE_SHIFT_KERNEL);
}

#else /* !CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

static inline bool personality_4kb_pages(unsigned long pers)
{
	return false;
}

static inline unsigned int mm_pte_shift(const struct mm_struct *mm)
{
	return PAGE_SHIFT_KERNEL;
}

static inline void mm_set_pte_shift(struct mm_struct *mm, unsigned int shift)
{
}

static inline void mm_init_pte_shift(struct mm_struct *mm,
				     const struct task_struct *p)
{
}

#endif /* CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

static inline unsigned long mm_pte_size(const struct mm_struct *mm)
{
	return _AC(1, UL) << mm_pte_shift(mm);
}

static inline unsigned long mm_pte_mask(const struct mm_struct *mm)
{
	return ~(mm_pte_size(mm) - 1);
}

static inline bool mm_is_compat(const struct mm_struct *mm)
{
	return mm_pte_shift(mm) != PAGE_SHIFT_KERNEL;
}

static inline unsigned long mm_pmd_size(const struct mm_struct *mm)
{
	return mm_is_compat(mm) ? PMD_SIZE_4KB : PMD_SIZE;
}

static inline unsigned long mm_pgdir_size(const struct mm_struct *mm)
{
	return mm_is_compat(mm) ? PGDIR_SIZE_4KB : PGDIR_SIZE;
}

static inline unsigned int mm_slices_per_page(const struct mm_struct *mm)
{
	return 1U << (PAGE_SHIFT_KERNEL - mm_pte_shift(mm));
}

static inline unsigned long mm_offset_in_page(const struct mm_struct *mm,
					      unsigned long addr)
{
	return addr & (mm_pte_size(mm) - 1);
}

static inline bool mm_pte_aligned(const struct mm_struct *mm,
				  unsigned long addr)
{
	return !mm_offset_in_page(mm, addr);
}

static inline unsigned long mm_pages_to_kb(const struct mm_struct *mm,
					   unsigned long nr)
{
	return nr << (mm_pte_shift(mm) - 10);
}

static inline unsigned long mm_phys_pfn(const struct mm_struct *mm,
					phys_addr_t x)
{
	return (unsigned long)(x >> mm_pte_shift(mm));
}

extern unsigned long stack_guard_gap;

static inline unsigned long mm_stack_guard_gap(const struct mm_struct *mm)
{
	return (stack_guard_gap >> PAGE_SHIFT_KERNEL) << mm_pte_shift(mm);
}
#endif /* _LINUX_P3S_MM_H */
