/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM memory

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_MEMORY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MEMORY_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_set_memory_x, TP_PROTO(unsigned long addr, int nr_pages), TP_ARGS(addr, nr_pages));
DECLARE_HOOK(android_vh_set_memory_nxrw, TP_PROTO(unsigned long addr, int nr_pages, bool rw), TP_ARGS(addr, nr_pages, rw));
#else
#define trace_android_vh_set_memory_x(addr, nr_pages)
#define trace_android_vh_set_memory_nxrw(addr, nr_pages, rw)
#endif

#endif /* _TRACE_HOOK_MEMORY_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
