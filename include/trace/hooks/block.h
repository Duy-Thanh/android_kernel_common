/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM block

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BLOCK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BLOCK_H

#include <linux/sbitmap.h>
#include <linux/blk-mq.h>
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(__GENKSYMS__) || !defined(CONFIG_BLOCK)
struct blk_mq_tags;
struct blk_mq_alloc_data;
struct blk_mq_tag_set;
struct bio;
struct bio_vec;
struct page;
struct request_queue;
struct request;
struct blk_mq_hw_ctx;
struct blk_plug;
struct task_struct;
#else
/* struct blk_mq_tags */
#include <../block/blk-mq-tag.h>
/* struct blk_mq_alloc_data */
#include <../block/blk-mq.h>
/* struct blk_mq_tag_set struct blk_mq_hw_ctx*/
#include <linux/blk-mq.h>
/* struct bio */
#include <linux/blk_types.h>
/* struct bio_vec */
#include <linux/bvec.h>
/* struct page */
#include <linux/mm_types.h>
/* struct request_queue struct request struct blk_plug;*/
#include <linux/blkdev.h>
/* struct task_struct */
#include <linux/sched.h>
#endif /* __GENKSYMS__ */

DECLARE_HOOK(android_vh_blk_alloc_rqs,
	TP_PROTO(size_t *rq_size, struct blk_mq_tag_set *set,
		struct blk_mq_tags *tags),
	TP_ARGS(rq_size, set, tags));

DECLARE_HOOK(android_vh_blk_rq_ctx_init,
	TP_PROTO(struct request *rq, struct blk_mq_tags *tags,
		struct blk_mq_alloc_data *data, u64 alloc_time_ns),
	TP_ARGS(rq, tags, data, alloc_time_ns));

DECLARE_RESTRICTED_HOOK(android_rvh_bio_free,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio), 1);

DECLARE_HOOK(android_vh_bio_uninit,
	TP_PROTO(bool *skip, struct bio *bio),
	TP_ARGS(skip, bio));

DECLARE_HOOK(android_vh_bio_clone_fast,
	TP_PROTO(struct bio *bio_src, struct bio *bio_dst),
	TP_ARGS(bio_src, bio_dst));

DECLARE_HOOK(android_vh_bio_try_merge_page,
	TP_PROTO(bool *skip, bool *status, struct bio *bio,
		 struct bio_vec *bv, struct page *page, unsigned int len,
		 unsigned int off, bool *same_page),
	TP_ARGS(skip, status, bio, bv, page, len, off, same_page));

DECLARE_RESTRICTED_HOOK(android_rvh_bio_endio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio), 1);

DECLARE_HOOK(android_vh_blk_bounce_clone_bio,
	TP_PROTO(struct bio *bio, struct bio *bio_src),
	TP_ARGS(bio, bio_src));

DECLARE_HOOK(android_vh_blk_mq_alloc_request,
	TP_PROTO(struct request_queue *q, struct blk_mq_alloc_data *data),
	TP_ARGS(q, data));

DECLARE_RESTRICTED_HOOK(android_rvh_internal_blk_mq_alloc_request,
	TP_PROTO(bool *skip, int *tag, struct blk_mq_alloc_data *data),
	TP_ARGS(skip, tag, data), 1);

DECLARE_HOOK(android_vh_blk_mq_alloc_request_hctx,
	TP_PROTO(struct request_queue *q, struct blk_mq_alloc_data *data),
	TP_ARGS(q, data));


DECLARE_HOOK(android_vh_internal_blk_mq_free_request,
	TP_PROTO(bool *skip, struct request *rq, struct blk_mq_hw_ctx *hctx),
	TP_ARGS(skip, rq, hctx));

DECLARE_HOOK(android_vh_blk_mq_free_request_pre,
	TP_PROTO(struct request *rq),
	TP_ARGS(rq));

DECLARE_HOOK(android_vh_blk_mq_free_request,
	TP_PROTO(struct request *rq),
	TP_ARGS(rq));

DECLARE_HOOK(android_vh_blk_mq_complete_request,
	TP_PROTO(bool *skip, struct request *rq),
	TP_ARGS(skip, rq));

DECLARE_HOOK(android_vh_blk_mq_start_request,
	TP_PROTO(struct request *rq),
	TP_ARGS(rq));

DECLARE_RESTRICTED_HOOK(android_rvh_blk_mq_requeue_request,
	TP_PROTO(struct request *rq),
	TP_ARGS(rq), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_blk_mq_add_to_requeue_list,
	TP_PROTO(bool *skip, struct request *rq, bool kick_requeue_list),
	TP_ARGS(skip, rq, kick_requeue_list), 1);

DECLARE_HOOK(android_vh_blk_mq_kick_requeue_list,
	TP_PROTO(bool *skip, struct request_queue *q),
	TP_ARGS(skip, q));

DECLARE_HOOK(android_vh_blk_mq_check_expired,
	TP_PROTO(bool *skip, struct request *rq),
	TP_ARGS(skip, rq));

DECLARE_HOOK(android_vh_blk_mq_get_driver_tag,
	TP_PROTO(struct request *rq),
	TP_ARGS(rq));

DECLARE_RESTRICTED_HOOK(android_rvh_blk_mq_delay_run_hw_queue,
	TP_PROTO(bool *skip, struct blk_mq_hw_ctx *hctx, bool async),
	TP_ARGS(skip, hctx, async), 1);

DECLARE_HOOK(android_vh_blk_mq_run_hw_queue,
	TP_PROTO(bool *need_run, struct blk_mq_hw_ctx *hctx),
	TP_ARGS(need_run, hctx));

DECLARE_HOOK(android_vh_blk_mq_insert_request,
	TP_PROTO(bool *skip, struct blk_mq_hw_ctx *hctx, struct request *rq),
	TP_ARGS(skip, hctx, rq));

DECLARE_HOOK(android_vh_blk_mq_free_rq_map,
	TP_PROTO(bool *skip, struct blk_mq_tags *tags),
	TP_ARGS(skip, tags));

DECLARE_RESTRICTED_HOOK(android_rvh_blk_mq_alloc_rq_map,
	TP_PROTO(bool *skip, struct blk_mq_tags **tags,
		struct blk_mq_tag_set *set, int node, unsigned int flags),
	TP_ARGS(skip, tags, set, node, flags), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_blk_mq_alloc_rq_map_ext,
	TP_PROTO(bool *skip, struct blk_mq_tags *tags,
		 struct blk_mq_tag_set *set, int node, unsigned int flags),
	TP_ARGS(skip, tags, set, node, flags), 1);

DECLARE_HOOK(android_vh_blk_mq_hctx_notify_dead,
	TP_PROTO(bool *skip, struct blk_mq_hw_ctx *hctx),
	TP_ARGS(skip, hctx));

DECLARE_RESTRICTED_HOOK(android_rvh_blk_mq_init_allocated_queue,
	TP_PROTO(struct request_queue *q),
	TP_ARGS(q), 1);

DECLARE_HOOK(android_vh_blk_mq_exit_queue,
	TP_PROTO(struct request_queue *q),
	TP_ARGS(q));

DECLARE_RESTRICTED_HOOK(android_rvh_blk_mq_alloc_tag_set,
	TP_PROTO(struct blk_mq_tag_set *set),
	TP_ARGS(set), 1);

DECLARE_HOOK(android_vh_blk_mq_update_nr_requests,
	TP_PROTO(bool *skip, struct request_queue *q),
	TP_ARGS(skip, q));

DECLARE_HOOK(android_vh_blk_poll_first,
	TP_PROTO(bool *skip, int *ret, struct request_queue *q),
	TP_ARGS(skip, ret, q));

DECLARE_HOOK(android_vh_blk_poll_second,
	TP_PROTO(struct request_queue *q),
	TP_ARGS(q));

DECLARE_HOOK(android_vh_blk_cleanup_queue,
	TP_PROTO(struct request_queue *q),
	TP_ARGS(q));

DECLARE_RESTRICTED_HOOK(android_rvh_blk_allocated_queue_init,
	TP_PROTO(bool *skip, struct request_queue *q),
	TP_ARGS(skip, q), 1);

DECLARE_HOOK(android_vh_blk_put_request,
	TP_PROTO(struct request *rq),
	TP_ARGS(rq));

DECLARE_RESTRICTED_HOOK(android_rvh_submit_bio_noacct,
	TP_PROTO(bool *skip, struct bio *bio),
	TP_ARGS(skip, bio), 1);

DECLARE_HOOK(android_vh_blk_insert_cloned_request,
	TP_PROTO(struct request_queue *q, struct request *rq),
	TP_ARGS(q, rq));

DECLARE_HOOK(android_vh_blk_account_io_completion,
	TP_PROTO(struct request *rq, unsigned int bytes),
	TP_ARGS(rq, bytes));

DECLARE_HOOK(android_vh_blk_update_request,
	TP_PROTO(struct request *rq, blk_status_t error, unsigned int nr_bytes),
	TP_ARGS(rq, error, nr_bytes));

DECLARE_HOOK(android_vh_blk_start_plug,
	TP_PROTO(struct task_struct *tsk, struct blk_plug *plug),
	TP_ARGS(tsk, plug));

DECLARE_RESTRICTED_HOOK(android_rvh_blk_flush_plug_list,
	TP_PROTO(struct blk_plug *plug, bool from_schedule),
	TP_ARGS(plug, from_schedule), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_blk_execute_rq_nowait,
	TP_PROTO(struct request_queue *q, struct request *rq,
		 rq_end_io_fn * done),
	TP_ARGS(q, rq, done), 1);

#endif /* _TRACE_HOOK_BLOCK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
