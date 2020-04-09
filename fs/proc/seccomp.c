// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/proc/seccomp.c
 * TODO: write better header
 * read back seccomp filter of a process
 */
#include <linux/file.h>
#include <linux/filter.h>
#include <linux/fs.h>
#include <linux/seccomp.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include "internal.h"

static struct proc_seccomp_private {
	struct task_struct *task;
};

static int seccomp_show(struct seq_file *m, void *v)
{
	struct seccomp_filter *filter = v;
	struct sock_fprog_kern *filter_prog = filter->prog->orig_prog;  // perhaps check for NULL?

	seq_write(m, &filter_prog->len, sizeof(filter_prog->len));
	seq_write(m, filter_prog->filter, bpf_classic_proglen(filter_prog));

	return 0;
}

static void *seccomp_start(struct seq_file *m, loff_t *ppos)
{
	struct proc_seccomp_private *priv = m->private;
	if (*ppos != 0)
                return ERR_PTR(-EINVAL);

	// pin the task
	priv->task = get_proc_task(file_inode(m->file));
	if (!priv->task)
		return ERR_PTR(-ESRCH);

	return READ_ONCE(priv->task->seccomp.filter);
}

static void *seccomp_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct seccomp_filter *filter = v;
	return filter->prev;
}

static void seccomp_stop(struct seq_file *m, void *v)
{
	struct proc_seccomp_private *priv = m->private;

	if (priv->task) {
		put_task_struct(priv->task);
		priv->task = NULL;
	}
}

static const struct seq_operations proc_pid_seccomp_ops = {
	.start  = seccomp_start,
	.next   = seccomp_next,
	.stop   = seccomp_stop,
	.show   = seccomp_show
};

static int pid_seccomp_open(struct inode *inode, struct file *file)
{
	return seq_open_private(file, &proc_pid_seccomp_ops,
				sizeof(struct proc_seccomp_private));
}

const struct file_operations proc_pid_seccomp_operations = {
	.open           = pid_seccomp_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = seq_release_private,
};
