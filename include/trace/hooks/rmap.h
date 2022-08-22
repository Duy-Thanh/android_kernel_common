/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM rmap

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_RMAP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_RMAP_H

#include <linux/tracepoint.h>
#include <linux/rmap.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_look_around,
	TP_PROTO(struct page_vma_mapped_walk *pvmw, struct page *page,
		struct vm_area_struct *vma, int *referenced),
	TP_ARGS(pvmw, page, vma, referenced));

#endif /* _TRACE_HOOK_RMAP_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
