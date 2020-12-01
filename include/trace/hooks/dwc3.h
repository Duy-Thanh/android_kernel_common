/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dwc3

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DWC3_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DWC3_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct dwc3;
struct dwc3_ep;

DECLARE_HOOK(android_vh_dwc3_conndone_interrupt,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_stop_transfers,
	TP_PROTO(struct dwc3_ep *dep),
	TP_ARGS(dep));

DECLARE_HOOK(android_vh_dwc3_stop_active_transfer,
	TP_PROTO(struct dwc3_ep *dep),
	TP_ARGS(dep));

DECLARE_HOOK(android_vh_dwc3_controller_halted,
	TP_PROTO(struct dwc3 *dwc, int is_on),
	TP_ARGS(dwc, is_on));

DECLARE_HOOK(android_vh_dwc3_free_event_buffers,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_alloc_event_buffers,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_setup_event_buffers,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_cleanup_event_buffers,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_post_core_init,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_pullup,
	TP_PROTO(struct dwc3 *dwc, int is_on),
	TP_ARGS(dwc, is_on));

DECLARE_HOOK(android_vh_dwc3_erratic_error,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_disconnect_interrupt,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_suspend_interrupt,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

DECLARE_HOOK(android_vh_dwc3_reset_interrupt,
	TP_PROTO(struct dwc3 *dwc),
	TP_ARGS(dwc));

#else
#define trace_android_vh_dwc3_conndone_interrupt(dwc)
#define trace_android_vh_dwc3_stop_transfers(dep)
#define trace_android_vh_dwc3_stop_active_transfer(dep)
#define trace_android_vh_dwc3_controller_halted(dwc, is_on)
#define trace_android_vh_dwc3_free_event_buffers(dwc)
#define trace_android_vh_dwc3_alloc_event_buffers(dwc)
#define trace_android_vh_dwc3_setup_event_buffers(dwc)
#define trace_android_vh_dwc3_cleanup_event_buffers(dwc)
#define trace_android_vh_dwc3_post_core_init(dwc)
#define trace_android_vh_dwc3_pullup(dwc, is_on)
#define trace_android_vh_dwc3_erratic_error(dwc)
#define trace_android_vh_dwc3_disconnect_interrupt(dwc)
#define trace_android_vh_dwc3_suspend_interrupt(dwc)
#define trace_android_vh_dwc3_reset_interrupt(dwc)

#endif
#endif /* _TRACE_HOOK_DWC3_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
