/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM security
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SECURITY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SECURITY_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct task_struct;
struct file;
struct dentry;
struct linux_binprm;
struct inode;
DECLARE_RESTRICTED_HOOK(android_vh_security_bprm_check,
	TP_PROTO(struct linux_binprm *bprm),
	TP_ARGS(bprm), 1);
DECLARE_HOOK(android_vh_security_inode_free,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode));
DECLARE_RESTRICTED_HOOK(android_vh_security_inode_setxattr,
	TP_PROTO(struct dentry *dentry, const char *name,
		 const void *value, size_t size, int flags),
	TP_ARGS(dentry, name, value, size, flags), 1);
DECLARE_HOOK(android_vh_security_inode_post_setxattr,
	TP_PROTO(struct dentry *dentry, const char *name,
		 const void *value, size_t size, int flags),
	TP_ARGS(dentry, name, value, size, flags));
DECLARE_HOOK(android_vh_security_inode_removexattr,
	TP_PROTO(struct dentry *dentry, const char *name),
	TP_ARGS(dentry, name));
DECLARE_RESTRICTED_HOOK(android_vh_security_file_open,
	TP_PROTO(struct file *file),
	TP_ARGS(file), 1);
DECLARE_HOOK(android_vh_security_file_free,
	TP_PROTO(struct file *file),
	TP_ARGS(file));
DECLARE_RESTRICTED_HOOK(android_vh_security_mmap_file,
	TP_PROTO(struct file *file, unsigned long prot, unsigned long flags),
	TP_ARGS(file, prot, flags), 1);
DECLARE_RESTRICTED_HOOK(android_vh_security_task_alloc,
	TP_PROTO(struct task_struct *task, unsigned long clone_flags, int *rc),
	TP_ARGS(task, clone_flags, rc), 1);
DECLARE_HOOK(android_vh_security_task_free,
	TP_PROTO(struct task_struct *task),
	TP_ARGS(task));
#else
#define trace_android_vh_security_bprm_check(bprm);
#define trace_android_vh_security_inode_free(inode);
#define trace_android_vh_security_inode_setxattr(dentry, name, value, ssize, \
						 flags)
#define trace_android_vh_security_inode_post_setxattr(dentry, name, value, \
						      ssize, flags)
#define trace_android_vh_security_inode_removexattr(dentry, name);
#define trace_android_vh_security_file_open(file);
#define trace_android_vh_security_file_free(file);
#define trace_android_vh_security_mmap_file(file, prot, flags);
#define trace_android_vh_security_task_alloc(task, clone_flags, rc);
#define trace_android_vh_security_task_free(task);
#endif
#endif /* _TRACE_HOOK_SECURITY_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
