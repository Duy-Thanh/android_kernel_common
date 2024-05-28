/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM kprobes

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_KPROBES_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_KPROBES_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_RESTRICTED_HOOK(android_rvh_get_insn_slot,
	TP_PROTO(const kprobe_opcode_t *insns),
	TP_ARGS(insns), 1);

#endif /* _TRACE_HOOK_KPROBES_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
