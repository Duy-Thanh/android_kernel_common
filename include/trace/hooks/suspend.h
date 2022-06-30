/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM suspend

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SUSPEND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SUSPEND_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_enter_resume_from_suspend,
        TP_PROTO(ktime_t start_time),
        TP_ARGS(start_time))
DECLARE_HOOK(android_vh_end_resume_from_suspend,
        TP_PROTO(ktime_t end_time),
        TP_ARGS(end_time))

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_SUSPEND_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

