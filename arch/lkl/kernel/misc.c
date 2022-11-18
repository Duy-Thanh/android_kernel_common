/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/kallsyms.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <asm/ptrace.h>
#include <asm/host_ops.h>

// This stack frame definition is only tested for amd64 build and there is no
// gurantee it works for other platforms.
struct stack_frame {
	struct stack_frame *next_frame;
	unsigned long return_address;
};

void show_stack(struct task_struct *task, unsigned long *sp,
				const char *loglvl)
{
	struct stack_frame *stack;
	unsigned int level = 0;
	unsigned long addr = 0;

	if (task != NULL && task != current) {
		pr_warn("Routine show_stack isn't implemented for non-current thread");
		return;
	}

	stack = __builtin_frame_address(0);
	pr_info("Linux Kernel Library Stack Trace:\n");
	while (stack) {
		addr = stack->return_address;
		pr_info("#%d [<0x%016lx>] %pS", level, addr, (void *)addr);
		pr_cont("\n");
		stack = stack->next_frame;
		level++;
	}
	pr_info("\n");
}

void lkl_arch_bug(const char* msg)
{
	pr_info("BUG: %s\n", msg);
	show_stack(/*task*/ NULL, /*sp*/ NULL, /*loglvl*/ NULL);
	panic("lkl_arch_bug");
}

void lkl_arch_warn(const char* msg)
{
	pr_info("WARN: %s\n", msg);
}

void show_regs(struct pt_regs *regs)
{
}

// TODO: workaround for b/259120931. Both wrong_size_cmpxchg and
// __generic_xchg_called_with_bad_pointer should be eliminated due to function
// inlining and -O2 optimization. But for some reason this doesn't happen. As temp
// fix define these symbols.
unsigned long wrong_size_cmpxchg(volatile void *ptr) {
	panic("wrong_size_cmpxchg");
}

void __generic_xchg_called_with_bad_pointer(void) {
	panic("__generic_xchg_called_with_bad_pointer");
}

#ifdef CONFIG_PROC_FS
static void *cpuinfo_start(struct seq_file *m, loff_t *pos)
{
	return NULL;
}

static void *cpuinfo_next(struct seq_file *m, void *v, loff_t *pos)
{
	return NULL;
}

static void cpuinfo_stop(struct seq_file *m, void *v)
{
}

static int show_cpuinfo(struct seq_file *m, void *v)
{
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start = cpuinfo_start,
	.next = cpuinfo_next,
	.stop = cpuinfo_stop,
	.show = show_cpuinfo,
};
#endif
