/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM blk

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_BLK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_BLK_H

#include <trace/hooks/vendor_hooks.h>

struct block_device;
struct gendisk;

DECLARE_HOOK(android_vh_bd_link_disk_holder,
	TP_PROTO(struct block_device *bdev, struct gendisk *disk),
	TP_ARGS(bdev, disk));
DECLARE_HOOK(android_vh_blk_fill_rwbs,
	TP_PROTO(char *rwbs, unsigned int opf),
	TP_ARGS(rwbs, opf));

struct path;
struct vfsmount;

DECLARE_HOOK(android_vh_do_new_mount_fc,
	TP_PROTO(struct path *mountpoint, struct vfsmount *mnt),
	TP_ARGS(mountpoint, mnt));

struct readahead_control;
typedef __u32 __bitwise blk_opf_t;

DECLARE_HOOK(android_vh_f2fs_ra_op_flags,
	TP_PROTO(blk_opf_t *op_flag, struct readahead_control *rac),
	TP_ARGS(op_flag, rac));

struct blk_mq_hw_ctx;
struct request_queue;

DECLARE_HOOK(android_vh_blk_mq_delay_run_hw_queue,
	TP_PROTO(int cpu, struct blk_mq_hw_ctx *hctx, unsigned long delay, bool *skip),
	TP_ARGS(cpu, hctx, delay, skip));

DECLARE_HOOK(android_vh_blk_mq_kick_requeue_list,
	TP_PROTO(struct request_queue *q, unsigned long delay, bool *skip),
	TP_ARGS(q, delay, skip));

struct bio;

DECLARE_HOOK(android_vh_check_set_ioprio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

<<<<<<< HEAD   (3f09c16da48bcd33c8300a6dd216da65d5ceaaa2 FROMGIT: f2fs: block cache/dio write during f2fs_enable_chec)
DECLARE_HOOK(android_vh_bio_add_page_merge_bypass,
	TP_PROTO(struct bio *bio, bool *skip),
	TP_ARGS(bio, skip));
||||||| BASE   (aa8a9034d8a0e18d37fff6c6053f470be77637ae ANDROID: loop: export loop_process_cmd_list)
=======
struct request;
DECLARE_HOOK(android_vh_loop_skip_queue_work,
        TP_PROTO(struct request *req, bool *skip),
        TP_ARGS(req, skip));
>>>>>>> CHANGE (8c6fbee830490fab45da6ef6c408a851d9cff88e ANDROID: vendor_hooks: add hook in loop_queue_work)

#endif /* _TRACE_HOOK_BLK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
