/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM blk

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BLK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BLK_H

#include <trace/hooks/vendor_hooks.h>

struct bio;
struct readahead_control;

DECLARE_HOOK(android_vh_f2fs_read_single_page,
	TP_PROTO(struct bio *bio, struct readahead_control *rac),
	TP_ARGS(bio, rac));

#endif /* _TRACE_HOOK_BLK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
