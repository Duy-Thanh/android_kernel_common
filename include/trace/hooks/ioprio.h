/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ioprio

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_IOPRIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IOPRIO_H

#include <trace/hooks/vendor_hooks.h>

struct bio;

DECLARE_HOOK(android_vh_bio_ioprio_acct,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

#endif /* _TRACE_HOOK_IOPRIO_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
