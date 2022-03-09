/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fs

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FS_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
struct task_struct;
DECLARE_HOOK(android_vh_ep_create_wakeup_source,
	TP_PROTO(char *name, int len),
	TP_ARGS(name, len));

DECLARE_HOOK(android_vh_timerfd_create,
	TP_PROTO(char *name, int len),
	TP_ARGS(name, len));

DECLARE_HOOK(android_vh_fdleak_detect,
	TP_PROTO(struct task_struct *task, int fd),
	TP_ARGS(task, fd));
#endif /* _TRACE_HOOK_FS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
