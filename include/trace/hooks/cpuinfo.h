/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cpuinfo

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_CPUINFO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CPUINFO_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_HOOK(android_vh_cpuinfo_get_hwinfo,
	TP_PROTO(const char **hwinfo),
	TP_ARGS(hwinfo));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_CPUINFO_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
