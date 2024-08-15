/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mem_report

#ifdef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_PATH
#endif
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MEM_REPORT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MEM_REPORT_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_get_log_usertype,
	TP_PROTO(unsigned int *type),
	TP_ARGS(type));

DECLARE_HOOK(android_vh_hievent_to_jank,
	TP_PROTO(int tag, int prio, const char *buf, int *ret),
	TP_ARGS(tag, prio, buf, ret));

DECLARE_RESTRICTED_HOOK(android_rvh_hiview_hievent_create,
	TP_PROTO(unsigned int event_id, void **event),
	TP_ARGS(event_id, event), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_hiview_hievent_put_string,
	TP_PROTO(void *event, const char *key, const char *value, int *ret),
	TP_ARGS(event, key, value, ret), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_hiview_hievent_put_integral,
	TP_PROTO(void *event, const char *key, long long value, int *ret),
	TP_ARGS(event, key, value, ret), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_hiview_hievent_report,
	TP_PROTO(void *event, int *ret),
	TP_ARGS(event, ret), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_hiview_hievent_destroy,
	TP_PROTO(void *event),
	TP_ARGS(event), 1);

#endif /* _TRACE_HOOK_MEM_REPORT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
