/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM domain_governor
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_DOMAIN_GOVERNOR_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DOMAIN_GOVERNOR_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct dev_power_governor;
DECLARE_HOOK(android_vh_domain_cpu_power_down_ok,
	TP_PROTO(struct dev_pm_domain *pd, bool *power_down_ok),
	TP_ARGS(pd, power_down_ok));

DECLARE_HOOK(android_vh_domain_default_suspend_ok,
	TP_PROTO(struct device *dev, bool *suspend_ok),
	TP_ARGS(dev, suspend_ok));

#endif /* _TRACE_HOOK_DOMAIN_GOVERNOR_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
