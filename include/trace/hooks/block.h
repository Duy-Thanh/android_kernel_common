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
struct blk_mq_alloc_data;
struct request_queue;
struct scsi_device;
struct Scsi_Host;

DECLARE_HOOK(android_vh_blk_alloc_rqs,
	TP_PROTO(int *bypass, size_t *rq_size, unsigned int cmd_size,
		struct blk_mq_tag_set *set, struct blk_mq_tags *tags),
	TP_ARGS(bypass, rq_size, cmd_size, set, tags));

DECLARE_HOOK(android_vh_blk_rq_ctx_init,
	TP_PROTO(struct request *rq, struct blk_mq_tags *tags,
		struct blk_mq_alloc_data *data, u64 alloc_time_ns),
	TP_ARGS(rq, tags, data, alloc_time_ns));

DECLARE_HOOK(android_vh_blk_rq_alloc_time,
	TP_PROTO(struct request_queue *q, u64 *alloc_time_ns),
	TP_ARGS(q, alloc_time_ns));

DECLARE_HOOK(android_vh_scsi_alloc_queue,
	TP_PROTO(struct scsi_device *sdev, struct request_queue *q),
	TP_ARGS(sdev, q));

DECLARE_HOOK(android_vh_scsi_setup_tags,
	TP_PROTO(struct Scsi_Host *shost, struct blk_mq_tag_set *tag_set),
	TP_ARGS(shost, tag_set));

#endif /* _TRACE_HOOK_BLOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
