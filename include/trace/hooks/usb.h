/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_HOOK(android_rvh_usb_tpmon_check_tp,
	TP_PROTO(int data_size),
	TP_ARGS(data_size));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_USB_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
