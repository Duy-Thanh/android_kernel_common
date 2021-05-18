/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mi_power
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MI_POWER_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MI_POWER_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct hlist_head;
struct irq_domain;
struct gic_chip_data;
struct alarm;
struct timerqueue_node;
struct timerfd_ctx;
struct spinlock;
struct list_head;
DECLARE_HOOK(android_vh_clk_debug_init,
	TP_PROTO(struct hlist_head *l),
	TP_ARGS(l));
DECLARE_HOOK(android_vh_gicv3_resume_init,
  TP_PROTO(struct gic_chip_data *gd),
  TP_ARGS(gd));
DECLARE_HOOK(android_vh_alarmtimer_enqueue,
	TP_PROTO(struct alarm* a),
	TP_ARGS(a));
DECLARE_HOOK(android_vh_alarmtimer_fired,
	TP_PROTO(struct alarm* a),
	TP_ARGS(a));
DECLARE_HOOK(android_vh_alarmtimer_suspend_a,
	TP_PROTO(struct timerqueue_node* n),
	TP_ARGS(n));
DECLARE_HOOK(android_vh_alarmtimer_suspend_b,
	TP_PROTO(int dummy),
	TP_ARGS(dummy));
DECLARE_HOOK(android_vh_timerfd_poll,
	TP_PROTO(struct timerfd_ctx *c),
	TP_ARGS(c));
DECLARE_HOOK(android_vh_wakeup_reason_init,
	TP_PROTO(struct spinlock *wl,struct list_head *p,struct list_head *l,int* wr, bool* cr,struct kmem_cache* wn),
	TP_ARGS(wl,p,l,wr,cr,wn));
DECLARE_HOOK(android_vh_regulator_info_init,
    TP_PROTO(struct class* device_info),
    TP_ARGS(device_info));
#else
#define trace_android_vh_clk_debug_init(l)
#define trace_android_vh_gicv3_resume_init(gd)
#define trace_android_vh_alarmtimer_enqueue(a)
#define trace_android_vh_alarmtimer_fired(a)
#define trace_android_vh_alarmtimer_suspend_a(n)
#define trace_android_vh_alarmtimer_suspend_b(dummy)
#define trace_android_vh_timerfd_poll(c)
#define trace_android_vh_wakeup_reason_init(wl,p,l,wr)
#define trace_android_vh_regulator_info_init(device_info)
#endif
#endif /* _TRACE_HOOK_MI_POWER_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
