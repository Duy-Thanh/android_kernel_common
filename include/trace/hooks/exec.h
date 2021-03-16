/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM exec

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_EXEC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_EXEC_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_set_task_comm,
	TP_PROTO(struct task_struct *tsk, bool exec),
	TP_ARGS(tsk, exec));
struct linux_binprm;
DECLARE_HOOK(android_vh_setup_new_exec,
	TP_PROTO(struct task_struct *tsk, struct linux_binprm *bprm),
	TP_ARGS(tsk, bprm));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_TOPOLOGY_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
