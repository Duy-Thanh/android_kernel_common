/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM topology

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_TOPOLOGY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TOPOLOGY_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_use_amu_fie,
	TP_PROTO(bool *use_amu_fie),
	TP_ARGS(use_amu_fie));

#endif /* _TRACE_HOOK_TOPOLOGY_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
