/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM block

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BLOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BLOCK_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct blk_mq_tag_set;
struct blk_mq_tags;

DECLARE_HOOK(android_vh_blk_alloc_rqs,
	TP_PROTO(int *bypass, size_t *rq_size, unsigned int cmd_size,
		struct blk_mq_tag_set *set, struct blk_mq_tags *tags),
	TP_ARGS(bypass, rq_size, cmd_size, set, tags));

#endif /* _TRACE_HOOK_BLOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
