/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bpf

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_BPF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BPF_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_HOOK(android_vh_set_permit_after_jit_compile,
	TP_PROTO(const void *addr, int nr_pages),
	TP_ARGS(addr, nr_pages));

#endif /* _TRACE_HOOK_BPF_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
