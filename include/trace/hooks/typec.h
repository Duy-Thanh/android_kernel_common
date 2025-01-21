/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM typec
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_TYPEC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_TYPEC_H
#include <trace/hooks/vendor_hooks.h>
struct tcpc_dev;

DECLARE_HOOK(android_vh_typec_pps_vbus_disc_thresh,
	     TP_PROTO(struct tcpc_dev *tcpc, u32 pps_apdo_min_voltage, int *ret, bool *bypass),
	     TP_ARGS(tcpc, pps_apdo_min_voltage, ret, bypass));

#endif /* _TRACE_HOOK_TYPEC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
