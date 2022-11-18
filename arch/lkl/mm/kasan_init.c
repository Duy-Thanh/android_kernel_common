// SPDX-License-Identifier: GPL-2.0
#define DISABLE_BRANCH_PROFILING

#include <linux/memblock.h>
#include <linux/kasan.h>
#include <linux/kdebug.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/vmalloc.h>

#ifdef CONFIG_KASAN
void __init kasan_early_init(void)
{
}

void __init kasan_init(void)
{
	init_task.kasan_depth = 0;

	// By default KASAN implementation seems to only WARN on detected
	// OOB issues. Turning this on so that we can crash the kernel when
	// issues are detected.
	panic_on_warn = 1;

	pr_info("KernelAddressSanitizer initialized\n");
}
#endif
