/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM msd_scsi

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MSD_SCSI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MSD_SCSI_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_msd_scsi_command,
	TP_PROTO(u8 *cmnd, int cmnd_size),
	TP_ARGS(cmnd, cmnd_size));

#endif /* _TRACE_HOOK_MSD_SCSI_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
