/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM scsi

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SCSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SCSI_H

#include <trace/hooks/vendor_hooks.h>

struct scsi_sense_hdr;
struct scsi_device;

DECLARE_RESTRICTED_HOOK(android_rvh__scsi_execute,
	TP_PROTO(bool *skip, int *ret, struct scsi_device *sdev,
		 const unsigned char *cmd, int data_direction, void *buffer,
		 unsigned bufflen, unsigned char *sense,
		 struct scsi_sense_hdr *sshdr, int timeout, int retries,
		 u64 flags, req_flags_t rq_flags, int *resid),
	TP_ARGS(skip, ret, sdev, cmd, data_direction, buffer, bufflen, sense,
		sshdr, timeout, retries, flags, rq_flags, resid), 1);

#endif /* _TRACE_HOOK_SCSI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
