/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM logbuf

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_LOGBUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_LOGBUF_H

#include <trace/hooks/vendor_hooks.h>

struct printk_ringbuffer;
struct printk_record;

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
DECLARE_RESTRICTED_HOOK(android_rvh_logbuf,
	TP_PROTO(struct printk_ringbuffer *rb, struct printk_record *r),
	TP_ARGS(rb, r), true)

>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
DECLARE_HOOK(android_vh_logbuf,
	TP_PROTO(struct printk_ringbuffer *rb, struct printk_record *r),
	TP_ARGS(rb, r))

DECLARE_HOOK(android_vh_logbuf_pr_cont,
	TP_PROTO(struct printk_record *r, size_t text_len),
	TP_ARGS(r, text_len))

#endif /* _TRACE_HOOK_LOGBUF_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
