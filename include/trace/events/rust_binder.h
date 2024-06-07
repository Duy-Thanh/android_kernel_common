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
	TP_PROTO(bool reply, rust_binder_transaction t),
	TP_ARGS(reply, t),
	TP_STRUCT__entry(
		__field(int, debug_id)
		__field(int, reply)
		__field(unsigned int, code)
		__field(unsigned int, flags)
	),
	TP_fast_assign(
		__entry->reply = reply;
		__entry->debug_id = rust_binder_transaction_debug_id(t);
		__entry->code = rust_binder_transaction_code(t);
		__entry->flags = rust_binder_transaction_flags(t);
	),
	TP_printk("transaction=%d reply=%d flags=0x%x code=0x%x",
		__entry->debug_id, __entry->reply, __entry->flags,
		__entry->code)
);

#endif /* _RUST_BINDER_TRACE_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
