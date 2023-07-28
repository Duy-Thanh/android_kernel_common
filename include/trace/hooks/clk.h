/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM clk
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_CLK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CLK_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_allow_clk_reparent,
	TP_PROTO(unsigned long flags, bool *bypass),
	TP_ARGS(flags, bypass))

#endif /* _TRACE_HOOK_CLK_H */

#include <trace/define_trace.h>
