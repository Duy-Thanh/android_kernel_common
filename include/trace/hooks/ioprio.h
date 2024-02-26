/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM ioprio 

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_IOPRIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IOPRIO_H

#include <trace/hooks/vendor_hooks.h>

struct bio;
struct page;

DECLARE_HOOK(android_vh_bio_set_ioprio,
	TP_PROTO(struct bio *bio, struct page *page),
	TP_ARGS(bio, page));
DECLARE_HOOK(android_vh_bio_set_ioprio_ipu,
	TP_PROTO(struct bio *bio, struct page *page),
	TP_ARGS(bio, page));
DECLARE_HOOK(android_vh_bio_set_ioprio_iter,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

#endif /* _TRACE_HOOK_IOPRIO_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
