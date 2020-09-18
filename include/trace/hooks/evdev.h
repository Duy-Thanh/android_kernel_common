/-License-Identifier: GPL-2.0 */CE_SYSTEM
#define TRACE_SYSTEM evdev

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_EVDEV_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_EVDEV_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
#include <linux/input.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
DECLARE_HOOK(android_vh_pass_input_event,
        TP_PROTO(const void *vals, unsigned int count),
        TP_ARGS(vals, count))
#else
#define trace_android_vh_pass_input_event(vals, count)
#endif

#endif /* _TRACE_HOOK_EVDEV_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

