/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM huge_memory

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_HUGE_MEMORY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_HUGE_MEMORY_H

#include <trace/hooks/vendor_hooks.h>

struct vm_area_struct;
DECLARE_HOOK(android_vh_thp_vma_allowable_orders,
	TP_PROTO(struct vm_area_struct *vma, unsigned long *orders),
	TP_ARGS(vma, orders));

#endif /* _TRACE_HOOK_HUGE_MEMORY_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
