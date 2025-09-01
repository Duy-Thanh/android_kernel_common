/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM printk

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PRINTK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PRINTK_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_printk_hotplug,
	TP_PROTO(int *flag),
	TP_ARGS(flag));

DECLARE_HOOK(android_vh_printk_caller_id,
	TP_PROTO(u32 *caller_id),
	TP_ARGS(caller_id));
DECLARE_HOOK(android_vh_printk_caller,
	TP_PROTO(char *caller, size_t size, u32 id, int *ret),
	TP_ARGS(caller, size, id, ret));
DECLARE_HOOK(android_vh_printk_ext_header,
	TP_PROTO(char *caller, size_t size, u32 id, int *ret),
	TP_ARGS(caller, size, id, ret));
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
DECLARE_HOOK(android_vh_printk_save_irq,
	TP_PROTO(u32 *caller_id, unsigned long irqflags),
	TP_ARGS(caller_id, irqflags));
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

#endif /* _TRACE_HOOK_PRINTK_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
