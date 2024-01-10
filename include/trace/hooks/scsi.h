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
struct scsi_host_template;
#else
/* struct Scsi_Host struct scsi_host_template */
#include <scsi/scsi_host.h>
#endif /* __GENKSYMS__ */

DECLARE_HOOK(android_vh_scsi_host_alloc,
	TP_PROTO(bool *skip, struct scsi_host_template *sht,
		 struct Scsi_Host *shost),
	TP_ARGS(skip, sht, shost));

DECLARE_HOOK(android_vh_scsi_mq_setup_tags,
	TP_PROTO(struct Scsi_Host *shost),
	TP_ARGS(shost));

#endif /* _TRACE_HOOK_SCSI_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
