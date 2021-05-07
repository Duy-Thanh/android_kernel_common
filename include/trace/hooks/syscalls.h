/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM syscalls
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SYSCALLS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SYSCALLS_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct pt_regs;
DECLARE_HOOK(android_vh_syscall_trace_enter,
	TP_PROTO(struct pt_regs *pt_regs),
	TP_ARGS(pt_regs));
DECLARE_HOOK(android_vh_syscall_trace_exit,
	TP_PROTO(struct pt_regs *pt_regs),
	TP_ARGS(pt_regs));
#endif /* _TRACE_HOOK_SYSCALLS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

