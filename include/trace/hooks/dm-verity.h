/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dm-verity

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DM_VERITY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DM_VERITY_H

#include <trace/hooks/vendor_hooks.h>

struct dm_verity_io;

DECLARE_HOOK(android_vh_verity_end_io_queue_work,
	TP_PROTO(struct dm_verity_io *io, bool *skip),
	TP_ARGS(io, skip));

#endif /* _TRACE_HOOK_DM_VERITY_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
