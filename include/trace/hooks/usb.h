/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_usb_profile,
	TP_PROTO(const char *function),
	TP_ARGS(function));

#endif /* _TRACE_HOOK_USB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
