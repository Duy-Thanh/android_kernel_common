/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM inputeventspacket

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_INPUTEVENTSPACKET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_INPUTEVENTSPACKET_H
#include <trace/hooks/vendor_hooks.h>
#include <linux/input.h>

struct input_dev;

DECLARE_HOOK(android_vh_inputevents_packet,
	TP_PROTO(struct input_dev *input_dev),
	TP_ARGS(input_dev));

#endif /* _TRACE_HOOK_INPUTEVENTSPACKET_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
