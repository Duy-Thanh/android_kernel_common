/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM typec
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_TYPEC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TYPEC_H
#include <trace/hooks/vendor_hooks.h>

#ifndef TYPEC_TIMER
#define TYPEC_TIMER
enum typec_timer {
	SINK_WAIT_CAP,
	SOURCE_OFF,
	CC_DEBOUNCE,
	SINK_DISCOVERY_BC12,
};
#endif

DECLARE_HOOK(android_vh_typec_tcpm_get_timer,
	     TP_PROTO(const char *state, enum typec_timer timer, unsigned int *msecs),
	     TP_ARGS(state, timer, msecs));

#endif /* _TRACE_HOOK_TYPEC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
