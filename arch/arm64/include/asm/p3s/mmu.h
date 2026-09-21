/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM64_P3S_MMU_H
#define _ASM_ARM64_P3S_MMU_H

#include <linux/p3s/mm.h>
#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>
#include <asm/processor.h>
#include <asm/sysreg.h>

#if defined(CONFIG_ARM64_64K_PAGES)
#define TCR_TG0_KERNEL		TCR_TG0_64K
#elif defined(CONFIG_ARM64_16K_PAGES)
#define TCR_TG0_KERNEL		TCR_TG0_16K
#else
#define TCR_TG0_KERNEL		TCR_TG0_4K
#endif

static inline unsigned long mm_tcr_geometry(const struct mm_struct *mm)
{
	if (mm_is_compat(mm))
		return TCR_TG0_4K | TCR_T0SZ(VA_BITS_4KB);

	return TCR_TG0_KERNEL | TCR_T0SZ(vabits_actual);
}

/*
 * mm_cpu_set_tcr_geometry - Update TCR_EL1 TG0 and T0SZ fields if changed
 * @geometry: Target TCR_TG0_* | TCR_T0SZ(...) bitmask
 */
static inline void mm_cpu_set_tcr_geometry(unsigned long geometry)
{
	unsigned long tcr = read_sysreg(tcr_el1);

	if ((tcr & (TCR_TG0_MASK | TCR_T0SZ_MASK)) == geometry)
		return;

	tcr &= ~(TCR_TG0_MASK | TCR_T0SZ_MASK);
	tcr |= geometry;
	write_sysreg(tcr, tcr_el1);
	isb();
}

static inline void mm_cpu_set_native_tcr_t0sz(unsigned long t0sz)
{
	mm_cpu_set_tcr_geometry(TCR_TG0_KERNEL | t0sz);
}

static inline unsigned long mm_task_size(const struct mm_struct *mm)
{
	return mm_is_compat(mm) ? TASK_SIZE_4KB : TASK_SIZE;
}

static inline unsigned long mm_default_map_window(const struct mm_struct *mm)
{
	return mm_is_compat(mm) ? DEFAULT_MAP_WINDOW_4KB : DEFAULT_MAP_WINDOW;
}

static inline unsigned long mm_stack_top_max(const struct mm_struct *mm)
{
	return mm_is_compat(mm) ? STACK_TOP_MAX_4KB : STACK_TOP_MAX;
}

static inline unsigned long mm_stack_top(const struct mm_struct *mm)
{
	return mm_is_compat(mm) ? STACK_TOP_4KB : STACK_TOP;
}

static inline unsigned long mm_task_unmapped_base(const struct mm_struct *mm)
{
	return mm_is_compat(mm) ? TASK_UNMAPPED_BASE_4KB :
				   PAGE_ALIGN_KERNEL(DEFAULT_MAP_WINDOW / 4);
}

static inline void mm_init_new_context(const struct task_struct *tsk,
				       struct mm_struct *mm)
{
	mm_init_pte_shift(mm, tsk);
}

static inline void mm_dup_mmap(const struct mm_struct *oldmm,
			       struct mm_struct *mm)
{
	mm_set_pte_shift(mm, mm_pte_shift(oldmm));
}

#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE

static inline void mm_switch_tcr(const struct mm_struct *mm)
{
	if (mm != &init_mm)
		mm_cpu_set_tcr_geometry(mm_tcr_geometry(mm));
}

#else /* !CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

static inline void mm_switch_tcr(const struct mm_struct *mm)
{
}

#endif /* CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

#endif /* _ASM_ARM64_P3S_MMU_H */
