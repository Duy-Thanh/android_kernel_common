/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM biosaver
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks


#if !defined(_TRACE_HOOK_BIOSAVER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BIOSAVER_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
struct bio;
DECLARE_HOOK(android_vh_biosaver_blk,
	TP_PROTO(struct bio *t),
	TP_ARGS(t));
DECLARE_HOOK(android_vh_biosaver_scsi,
	TP_PROTO(int count),
	TP_ARGS(count));


#endif /* _TRACE_HOOK_BIOSAVER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

