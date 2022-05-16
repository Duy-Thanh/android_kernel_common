/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM cgroup
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_CGROUP_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_CGROUP_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct task_struct;
DECLARE_HOOK(android_vh_cgroup_set_task,
	TP_PROTO(int ret, struct task_struct *task),
	TP_ARGS(ret, task));

struct mem_cgroup;
DECLARE_HOOK(android_rvh_memcgv2_init,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));

DECLARE_HOOK(android_rvh_memcgv2_calc_decayed_watermark,
	TP_PROTO(struct mem_cgroup *memcg, unsigned long *emin, unsigned long *elow),
	TP_ARGS(memcg, emin, elow));

struct page_counter;
DECLARE_HOOK(android_rvh_update_watermark,
	TP_PROTO(u64 new, struct page_counter *counter),
	TP_ARGS(new, counter));
#endif

#include <trace/define_trace.h>
