/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM suspend

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_SUSPEND_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SUSPEND_H

#include <trace/hooks/vendor_hooks.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
DECLARE_HOOK(android_vh_resume_begin,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
DECLARE_HOOK(android_vh_resume_end,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

DECLARE_HOOK(android_vh_early_resume_begin,
	TP_PROTO(void *unused),
	TP_ARGS(unused));

/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_SUSPEND_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
