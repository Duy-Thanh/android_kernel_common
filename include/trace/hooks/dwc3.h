/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dwc3

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DWC3_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DWC3_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
#include <linux/usb/ch9.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct dwc3;
struct dwc3_ep;

DECLARE_HOOK(android_vh___dwc3_gadget_ep_enable,
	TP_PROTO(struct dwc3_ep *dep, unsigned int action),
	TP_ARGS(dep, action));

DECLARE_HOOK(android_vh_dwc3_ep0_set_config,
	TP_PROTO(struct dwc3 *dwc, enum usb_device_state state),
	TP_ARGS(dwc, state));

#else
#define trace_android_vh___dwc3_gadget_ep_enable(dep, action)
#define trace_android_vh_dwc3_ep0_set_config(dwc, ctrl)

#endif
#endif /* _TRACE_HOOK_DWC3_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
