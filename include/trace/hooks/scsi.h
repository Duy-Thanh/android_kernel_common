/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM scsi
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SCSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SCSI_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * provide a mechanism for vendor modules to hook and extend functionality
 */
struct scsi_device;
DECLARE_RESTRICTED_HOOK(android_vh_scsi_send_cmd,
	TP_PROTO(struct scsi_device *sdv),
	TP_ARGS(sdv));

struct scsi_request;
DECLARE_RESTRICTED_HOOK(android_vh_scsi_set_request,
	TP_PROTO(unsigned char opcode, struct scsi_request *rq),
	TP_ARGS(opcode, rq));
/* macro versions of hooks are no longer required */
#endif /* _TRACE_HOOK_SCSI_SD_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
