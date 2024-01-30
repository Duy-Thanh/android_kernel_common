/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rmap

#if !defined(_TRACE_RMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RMAP_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(mm_rmap_page_referenced,
	TP_PROTO(ktime_t nsecs, int64_t pages_scanned, int64_t vma_pages_scanned),

	TP_ARGS(nsecs, pages_scanned, vma_pages_scanned),

	TP_STRUCT__entry(
		__field(ktime_t,	nsecs)
		__field(int64_t,	pages_scanned)
		__field(int64_t,	vma_pages_scanned)
	),

	TP_fast_assign(
		__entry->nsecs	= nsecs;
		__entry->pages_scanned	= pages_scanned;
		__entry->vma_pages_scanned	= vma_pages_scanned;
	),

	TP_printk("nsecs=%lld pages_scanned=%lld vma_pages_scanned=%lld",
		  __entry->nsecs, __entry->pages_scanned, __entry->vma_pages_scanned)
);
#endif /* _TRACE_RMAP_H */

#undef TRACE_INCLUDE_PATH

/* This part must be outside protection */
#include <trace/define_trace.h>
