/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct usb_function_instance;

DECLARE_HOOK(android_vh_config_usb_cfg_link,
	TP_PROTO(struct usb_function_instance *fi, int ret),
	TP_ARGS(fi, ret));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_USB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

