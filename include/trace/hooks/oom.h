#undef TRACE_SYSTEM
#define TRACE_SYSTEM oom

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_OOM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_OOM_H

#include <trace/hooks/vendor_hooks.h>

struct task_struct;

DECLARE_HOOK(android_vh_oom_get_victim,
	TP_PROTO(struct task_struct *victim),
	TP_ARGS(victim));

#endif /* _TRACE_HOOK_OOM_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
