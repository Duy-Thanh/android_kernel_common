/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FS_H

#include <trace/hooks/vendor_hooks.h>

#include <linux/fs.h>
DECLARE_HOOK(android_vh_do_open_exec,
	TP_PROTO(struct file *f),
	TP_ARGS(f));
DECLARE_HOOK(android_vh_do_fs_read,
	TP_PROTO(struct file *f, loff_t *pos,
		size_t count),
	TP_ARGS(f, pos, count));

#endif /* _TRACE_HOOK_FS_H */
#include <trace/define_trace.h>
