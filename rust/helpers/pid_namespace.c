// SPDX-License-Identifier: GPL-2.0

#include <linux/pid_namespace.h>
#include <linux/cleanup.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper struct pid_namespace *
rust_helper_get_pid_ns(struct pid_namespace *ns)
{
	return get_pid_ns(ns);
}

__rust_helper void rust_helper_put_pid_ns(struct pid_namespace *ns)
{
	put_pid_ns(ns);
}

/* Get a reference on a task's pid namespace. */
__rust_helper struct pid_namespace *
rust_helper_task_get_pid_ns(struct task_struct *task)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
struct pid_namespace *rust_helper_get_pid_ns(struct pid_namespace *ns)
{
	return get_pid_ns(ns);
}

void rust_helper_put_pid_ns(struct pid_namespace *ns)
{
	put_pid_ns(ns);
}

/* Get a reference on a task's pid namespace. */
struct pid_namespace *rust_helper_task_get_pid_ns(struct task_struct *task)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	struct pid_namespace *pid_ns;

	guard(rcu)();
	pid_ns = task_active_pid_ns(task);
	if (pid_ns)
		get_pid_ns(pid_ns);
	return pid_ns;
}
