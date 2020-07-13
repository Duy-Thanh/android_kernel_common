// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM isolate

#if !defined(_TRACE_ISOLATE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ISOLATE_H

#include <linux/sched/clock.h>
#include <linux/tracepoint.h>

/*
 * sched_isolate - called when cores are isolated/unisolated
 *
 * @acutal_mask: mask of cores actually isolated/unisolated
 * @req_mask: mask of cores requested isolated/unisolated
 * @online_mask: cpu online mask
 * @time: amount of time in us it took to isolate/unisolate
 * @isolate: 1 if isolating, 0 if unisolating
 *
 */
TRACE_EVENT(sched_isolate,

	TP_PROTO(unsigned int requested_cpu, unsigned int isolated_cpus,
		u64 start_time, unsigned char isolate),

	TP_ARGS(requested_cpu, isolated_cpus, start_time, isolate),

	TP_STRUCT__entry(
		__field(u32, requested_cpu)
		__field(u32, isolated_cpus)
		__field(u32, time)
		__field(unsigned char, isolate)
	),

	TP_fast_assign(
		__entry->requested_cpu = requested_cpu;
		__entry->isolated_cpus = isolated_cpus;
		__entry->time = div64_u64(sched_clock() - start_time, 1000);
		__entry->isolate = isolate;
	),

	TP_printk("iso cpu=%u cpus=0x%x time=%u us isolated=%d",
		__entry->requested_cpu, __entry->isolated_cpus,
		__entry->time, __entry->isolate)
);
#endif /* _TRACE_ISOLATE_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE trace

#include <trace/define_trace.h>
