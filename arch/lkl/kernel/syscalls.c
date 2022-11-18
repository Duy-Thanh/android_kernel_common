/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/stat.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/jhash.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/task_work.h>
#include <linux/syscalls.h>
#include <linux/kthread.h>
#include <linux/platform_device.h>
#include <asm/host_ops.h>
#include <asm/syscalls.h>
#include <asm/syscalls_32.h>
#include <asm/cpu.h>
#include <asm/sched.h>


typedef long (*syscall_handler_t)(long arg1, ...);

// First include of <asm/unistd.h> to generate prototypes of sys_xxx routines.
#undef __SYSCALL
#define __SYSCALL(nr, sym) asmlinkage long sym(long arg1, ...);

#include <asm/unistd.h>

#if __BITS_PER_LONG == 32
#include <asm/unistd_32.h>
#endif

// Second include of <asm/unistd.h> to initialize syscall_table with
// sys_xxx routines.
#undef __SYSCALL
#define __SYSCALL(nr, sym) [nr] = (syscall_handler_t)sym,

syscall_handler_t syscall_table[__NR_syscalls] = {
	[0 ... __NR_syscalls - 1] = (syscall_handler_t)sys_ni_syscall,
#include <asm/unistd.h>

#if __BITS_PER_LONG == 32
#include <asm/unistd_32.h>
#endif
};

static long run_syscall(long no, long *params)
{
	long ret;

	if (no < 0 || no >= __NR_syscalls)
		return -ENOSYS;

	ret = syscall_table[no](params[0], params[1], params[2], params[3],
				params[4], params[5]);

	task_work_run();

	return ret;
}

#define CLONE_FLAGS                                                            \
	(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_THREAD | CLONE_SIGHAND |    \
	 SIGCHLD)

static int host_task_id;
static struct task_struct *host0;

static unsigned long get_task_flags(void *task)
{
	return (unsigned long)task & LKL_TASK_FLAG_MASK;
}

static int new_host_task(struct task_struct **task)
{
	pid_t pid;
	unsigned long flags = CLONE_FLAGS;

	switch_to_host_task(host0);

	if (get_task_flags(*task) & LKL_TASK_NEW_TGID)
		flags &= ~CLONE_THREAD;

	pid = kernel_thread(host_task_stub, NULL, flags);
	if (pid < 0)
		return pid;

	rcu_read_lock();
	*task = find_task_by_pid_ns(pid, &init_pid_ns);
	rcu_read_unlock();

	host_task_id++;

	snprintf((*task)->comm, sizeof((*task)->comm), "host%d", host_task_id);

	return 0;
}

static void del_host_task(void *arg)
{
	struct task_struct *task = (struct task_struct *)arg;

	if (lkl_cpu_get() < 0)
		return;

	switch_to_host_task(task);
	host_task_id--;
	thread_exit_jb();
}

static struct lkl_tls_key *task_key;

bool is_task_set(void *task)
{
	return (unsigned long)task & ~LKL_TASK_FLAG_MASK;
}

long lkl_syscall(long no, long *params)
{
	struct task_struct *task = host0;
	long ret;

#ifdef CONFIG_KASAN
	// map memory for shadow the stack of the current thread
	BUG_ON(lkl_ops->kasan_map_thread_shadow_stack(lkl_ops->thread_self()) !=
	       0);
#endif
	ret = lkl_cpu_get();
	if (ret < 0)
		return ret;

	if (lkl_ops->tls_get) {
		task = lkl_ops->tls_get(task_key);
		if (!is_task_set(task)) {
			ret = new_host_task(&task);
			if (ret)
				goto out;
			lkl_ops->tls_set(task_key, task);
		}
	}

	switch_to_host_task(task);

	ret = run_syscall(no, params);

	if (no == __NR_reboot) {
		thread_sched_jb();
		return ret;
	}

out:
	lkl_cpu_put();

	return ret;
}

void *lkl_get_task(void)
{
	if (lkl_ops->tls_get) {
		return lkl_ops->tls_get(task_key);
	}
	return NULL;
}

int lkl_set_task(void *task)
{
	if (lkl_ops->tls_set) {
		return lkl_ops->tls_set(task_key, task);
	}
	return -1;
}

int lkl_set_task_flag(unsigned long flag)
{
	if (lkl_ops->tls_set) {
		flag &= LKL_TASK_FLAG_MASK;
		return lkl_ops->tls_set(task_key, (void *)flag);
	}
	return -1;
}

static struct task_struct *idle_host_task;

/* called from idle, don't failed, don't block */
void wakeup_idle_host_task(void)
{
	if (!need_resched() && idle_host_task)
		wake_up_process(idle_host_task);
}

static int idle_host_task_loop(void *unused)
{
	struct thread_info *ti = task_thread_info(current);

	snprintf(current->comm, sizeof(current->comm), "idle_host_task");
	set_thread_flag(TIF_HOST_THREAD);
	idle_host_task = current;

	for (;;) {
		lkl_cpu_put();
		lkl_ops->sem_down(ti->sched_sem);
		if (idle_host_task == NULL) {
			lkl_ops->thread_exit();
			return 0;
		}
		schedule_tail(ti->prev_sched);
	}
}

int syscalls_init(void)
{
	snprintf(current->comm, sizeof(current->comm), "host0");
	set_thread_flag(TIF_HOST_THREAD);
	host0 = current;

	// Reap zombie host tasks spawned with LKL_TASK_NEW_TGID
	kernel_sigaction(SIGCHLD, SIG_IGN);

	if (lkl_ops->tls_alloc) {
		task_key = lkl_ops->tls_alloc(del_host_task);
		if (!task_key)
			return -1;
	}

	if (kernel_thread(idle_host_task_loop, NULL, CLONE_FLAGS) < 0) {
		if (lkl_ops->tls_free)
			lkl_ops->tls_free(task_key);
		return -1;
	}

	return 0;
}

void syscalls_cleanup(void)
{
	if (idle_host_task) {
		struct thread_info *ti = task_thread_info(idle_host_task);

		idle_host_task = NULL;
		lkl_ops->sem_up(ti->sched_sem);
		lkl_ops->thread_join(ti->tid);
	}

	if (lkl_ops->tls_free)
		lkl_ops->tls_free(task_key);
}
