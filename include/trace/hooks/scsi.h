/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM scsi

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SCSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SCSI_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(__GENKSYMS__) || !defined(CONFIG_BLOCK)
struct Scsi_Host;
struct scsi_cmnd;
struct request_queue;
struct request;
#else
/* struct Scsi_Host struct scsi_host_template */
#include <scsi/scsi_host.h>
/* struct scsi_cmnd */
#include <scsi/scsi_cmnd.h>
/* struct request_queue struct request */
#include <linux/blkdev.h>
#endif /* __GENKSYMS__ */

DECLARE_HOOK(android_vh_scsi_host_alloc,
	TP_PROTO(bool *skip, struct scsi_host_template *sht,
		 struct Scsi_Host *shost),
	TP_ARGS(skip, sht, shost));

DECLARE_HOOK(android_vh_scsi_host_queue_ready,
	TP_PROTO(bool *skip, struct Scsi_Host *shost),
	TP_ARGS(skip, shost));

DECLARE_HOOK(android_vh_scsi_softirq_done,
	TP_PROTO(struct scsi_cmnd *cmd),
	TP_ARGS(cmd));

DECLARE_HOOK(android_vh_scsi_mq_put_budget,
	TP_PROTO(struct request_queue *q),
	TP_ARGS(q));

DECLARE_HOOK(android_vh_scsi_mq_get_budget,
	TP_PROTO(struct request_queue *q),
	TP_ARGS(q));

DECLARE_HOOK(android_vh_scsi_queue_rq_first,
	TP_PROTO(blk_status_t *ret, bool *skip, struct request *req,
		 struct Scsi_Host *shost),
	TP_ARGS(ret, skip, req, shost));

DECLARE_HOOK(android_vh_scsi_queue_rq_second,
	TP_PROTO(bool *skip, struct Scsi_Host *shost),
	TP_ARGS(skip, shost));

DECLARE_HOOK(android_vh_scsi_mq_setup_tags,
	TP_PROTO(struct Scsi_Host *shost, unsigned int cmd_size),
	TP_ARGS(shost, cmd_size));

DECLARE_HOOK(android_vh_scsi_mq_inline_sgl_size,
	TP_PROTO(unsigned int *ret, struct Scsi_Host *shost),
	TP_ARGS(ret, shost));

#endif /* _TRACE_HOOK_SCSI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
