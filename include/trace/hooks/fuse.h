/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fuse

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(TRACE_HOOKS_FUSE_H_) || defined(TRACE_HEADER_MULTI_READ)
#define TRACE_HOOKS_FUSE_H_

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_fuse_tmpfile,
	TP_PROTO(struct user_namespace *mnt_userns, struct inode *dir,
		 struct dentry *entry, umode_t mode, int* rtn),
	TP_ARGS(mnt_userns, dir, entry, mode,  rtn));

#endif /* TRACE_HOOKS_FUSE_H_ */

/* This part must be outside protection */
#include <trace/define_trace.h>
