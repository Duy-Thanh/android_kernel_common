/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hid
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_HID_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_HID_H
#include <trace/hooks/vendor_hooks.h>
/* struct hid_input / hid_field / hid_usage */
#include <linux/hid.h>
DECLARE_HOOK(android_vh_hidinput_pre_configure_usage,
	TP_PROTO(struct hid_input *hidinput,
		 struct hid_field *field,
		 struct hid_usage *usage,
		 unsigned long **bit,
		 int *max,
		 bool *is_reconfigured),
	TP_ARGS(hidinput, field, usage, bit, max, is_reconfigured));
#endif /* _TRACE_HOOK_HID_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
