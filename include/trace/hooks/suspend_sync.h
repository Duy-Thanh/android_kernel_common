/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM suspend_sync
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SUSPEND_SYNC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SUSPEND_SYNC_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_suspend_sync_start,
	TP_PROTO(int *retp),
	TP_ARGS(retp));
DECLARE_HOOK(android_vh_suspend_sync_end,
	TP_PROTO(int *retp),
	TP_ARGS(retp));
#else
static inline void trace_android_vh_suspend_sync_start(int *retp) {}
static inline void trace_android_vh_suspend_sync_end(int *retp) {}
#endif
#endif /* _TRACE_HOOK_SUSPEND_SYNC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
