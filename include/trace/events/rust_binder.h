/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rust_binder

#if !defined(_RUST_BINDER_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _RUST_BINDER_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(rust_binder_transaction,
	TP_PROTO(int debug_id, bool reply),
	TP_ARGS(debug_id, reply),
	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, reply)
	),
	TP_fast_assign(
		__entry->debug_id = debug_id;
		__entry->reply = reply;
	),
	TP_printk("transaction=%d reply=%d", __entry->debug_id, __entry->reply)
);

#endif /* _RUST_BINDER_TRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
