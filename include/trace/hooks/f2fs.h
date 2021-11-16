/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM f2fs
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_F2FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_F2FS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
DECLARE_HOOK(android_vh_f2fs_issue_discard_terminate,
	TP_PROTO(struct task_struct *task, bool *stop),
	TP_ARGS(task, stop));
DECLARE_HOOK(android_vh_f2fs_fstrim_need_wait,
	TP_PROTO(bool *wait),
	TP_ARGS(wait));
#endif /* _TRACE_HOOK_F2FS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
