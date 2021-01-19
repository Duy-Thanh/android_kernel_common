/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM list_debug
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_LIST_DEBUG_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_LIST_DEBUG_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct list_head;
DECLARE_RESTRICTED_HOOK(android_rvh_list_add_corruption,
	TP_PROTO(struct list_head *new, struct list_head *prev, struct list_head *next),
	TP_ARGS(new, prev, next), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_list_del_entry_corruption,
	TP_PROTO(struct list_head *entry),
	TP_ARGS(entry), 1);

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_LIST_DEBUG_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
