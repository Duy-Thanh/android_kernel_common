/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_SCHED_H
#define _ASM_LKL_SCHED_H

#include <linux/sched.h>
#include <linux/kasan.h>
#include <uapi/asm/host_ops.h>

static inline void thread_sched_jb(void)
{
#ifdef CONFIG_KASAN
#define KASAN_SHADOW_SCALE_MASK ((1 << KASAN_SHADOW_SCALE_SHIFT) - 1)
	char tmp[8];
	// kasan_unpoison_range below expects kasan-granule-size-aligned pointer
	void *kasan_aligned_ptr =
		(void *)((unsigned long long)(tmp + KASAN_SHADOW_SCALE_MASK) &
			 ~KASAN_SHADOW_SCALE_MASK);
#endif
	if (test_ti_thread_flag(current_thread_info(), TIF_HOST_THREAD)) {
		set_ti_thread_flag(current_thread_info(), TIF_SCHED_JB);
		set_current_state(TASK_UNINTERRUPTIBLE);
		lkl_ops->jmp_buf_set(&current_thread_info()->sched_jb,
				     schedule);
#ifdef CONFIG_KASAN
		// The prevois call to setjmp/longjmp won't unwind the stack
		// and, as a result, shadow memory will remain poisoned.
		// This is to manually unpoison the memory.
		// TODO: is there better way to unpoison the memory?
		kasan_unpoison_range(kasan_aligned_ptr - PAGE_SIZE * 2,
				     PAGE_SIZE * 2);
#endif
	} else {
		lkl_bug("thread_sched_jb() can be used only for host task\n");
	}
}

static void exit_task_stub(void)
{
	do_exit(0);
}

static inline void thread_exit_jb(void)
{
#ifdef CONFIG_KASAN
#define KASAN_SHADOW_SCALE_MASK ((1 << KASAN_SHADOW_SCALE_SHIFT) - 1)
	char tmp[8];
	void *kasan_aligned_ptr =
		(void *)((unsigned long long)(tmp + KASAN_SHADOW_SCALE_MASK) &
			 ~KASAN_SHADOW_SCALE_MASK);
#endif
	if (test_ti_thread_flag(current_thread_info(), TIF_HOST_THREAD)) {
		set_ti_thread_flag(current_thread_info(), TIF_SCHED_JB);
		set_current_state(TASK_UNINTERRUPTIBLE);
		lkl_ops->jmp_buf_set(&current_thread_info()->sched_jb,
				     exit_task_stub);
#ifdef CONFIG_KASAN
		// Manually unpoison the memory.
		// TODO: Replace this with a better way same as above
		kasan_unpoison_range(kasan_aligned_ptr - PAGE_SIZE * 2,
				     PAGE_SIZE * 2);
#endif
	} else {
		lkl_bug("thread_sched_jb() can be used only for host task\n");
	}
}

void switch_to_host_task(struct task_struct *);
int host_task_stub(void *unused);

#endif /*  _ASM_LKL_SCHED_H */
