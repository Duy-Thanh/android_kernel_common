/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FS_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_ksys_umount,
		TP_PROTO(char __user *name, int flags),
		TP_ARGS(name, flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_filemap_list,
		TP_PROTO(struct task_struct *task, struct inode *inode,
			struct file *file, const struct seq_operations *ops,
			int *ret),
		TP_ARGS(task, inode, file, ops, ret), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_io_record_read,
		TP_PROTO(char __user *buf, size_t count, loff_t *ppos,
			ssize_t *ret),
		TP_ARGS(buf, count, ppos, ret), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_io_record_write,
		TP_PROTO(struct task_struct *task, const char __user *buf,
			size_t count, ssize_t *ret),
		TP_ARGS(task, buf, count, ret), 1);
DECLARE_HOOK(android_vh_f2fs_file_open,
	TP_PROTO(struct inode *inode, struct file *filp),
	TP_ARGS(inode, filp));
DECLARE_HOOK(android_vh_proc_pid_ioinfo,
	TP_PROTO(struct seq_file *m, struct pid_namespace *ns,
		struct pid *pid, struct task_struct *task, int *ret),
	TP_ARGS(m, ns, pid, task, ret));
DECLARE_HOOK(android_vh_show_filemap,
	TP_PROTO(struct seq_file *m, struct vm_area_struct *vma),
	TP_ARGS(m, vma));
DECLARE_HOOK(android_vh_filemap_release,
	TP_PROTO(struct inode *inode, struct file *file, int *ret),
	TP_ARGS(inode, file, ret));

#endif /* _TRACE_HOOK_FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
