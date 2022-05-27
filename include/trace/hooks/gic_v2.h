/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gic_v2

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_GIC_V2_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_GIC_V2_H

#include <linux/irqdomain.h>
#include <trace/hooks/vendor_hooks.h>

struct gic_chip_data;

DECLARE_HOOK(android_vh_gic_v2_resume,
       TP_PROTO(struct gic_chip_data *gd),
       TP_ARGS(gd));

#endif /* _TRACE_HOOK_GIC_V2_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
