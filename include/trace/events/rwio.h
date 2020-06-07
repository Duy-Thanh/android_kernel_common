/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM rwio

#if !defined(_TRACE_RWIO_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RWIO_H

#include <linux/tracepoint.h>

TRACE_EVENT(raw_write,

	TP_PROTO(unsigned long fn, volatile void __iomem *addr),

	TP_ARGS(fn, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS write addr=%p\n", __entry->fn, __entry->addr)
);

TRACE_EVENT(raw_read,

	TP_PROTO(unsigned long fn, const volatile void __iomem *addr),

	TP_ARGS(fn, addr),

	TP_STRUCT__entry(
		__field(u64, fn)
		__field(u64, addr)
	),

	TP_fast_assign(
		__entry->fn = fn;
		__entry->addr = (u64)addr;
	),

	TP_printk("%pS read addr=%p\n", __entry->fn, __entry->addr)
);

#endif /* _TRACE_PREEMPTIRQ_H */

#include <trace/define_trace.h>
