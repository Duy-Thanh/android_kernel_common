/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rcu
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_RCU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_RCU_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_RESTRICTED_HOOK(android_rvh_sync_rcu_expedited_wait,
	TP_PROTO(const char *name),
	TP_ARGS(name), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_rcu_print_other_cpu_stall,
	TP_PROTO(const char *name),
	TP_ARGS(name), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_rcu_print_cpu_stall,
	TP_PROTO(const char *name),
	TP_ARGS(name), 1);

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_RCU_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
