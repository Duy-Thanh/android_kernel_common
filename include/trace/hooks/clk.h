/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM clk
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_CLK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CLK_H
#include <trace/hooks/vendor_hooks.h>

struct clk_hw;

DECLARE_HOOK(android_vh_clk_enable,
	TP_PROTO(struct clk_hw *hw),
	TP_ARGS(hw));

DECLARE_HOOK(android_vh_clk_disable,
	TP_PROTO(struct clk_hw *hw),
	TP_ARGS(hw));

DECLARE_HOOK(android_vh_clk_set_rate,
	TP_PROTO(struct clk_hw *hw, unsigned long rate),
	TP_ARGS(hw, rate));

#endif /* _TRACE_HOOK_CLK_H*/
/* This part must be outside protection */
#include <trace/define_trace.h>
