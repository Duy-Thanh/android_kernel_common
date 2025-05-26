/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM io_monitor

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_IO_MONITOR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IO_MONITOR_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_iomap_dio_bio_end_io,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

DECLARE_HOOK(android_vh_verity_end_io,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

DECLARE_HOOK(android_vh_f2fs_read_end_io,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

DECLARE_HOOK(android_vh_f2fs_write_end_io,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));

DECLARE_HOOK(android_vh_z_erofs_submissionqueue_endio,
	TP_PROTO(struct bio *bio),
	TP_ARGS(bio));
#endif /* _TRACE_HOOK_IO_MONITOR_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
