/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_MMU_CONTEXT_H
#define _ASM_LKL_MMU_CONTEXT_H

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
		      struct task_struct *tsk) {

}

/*  Generic hooks for arch_dup_mmap and arch_exit_mmap  */
#include <asm-generic/mm_hooks.h>
#include <asm-generic/mmu_context.h>
#endif
