/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GPU trace points
 *
 * Copyright (C) 2024 Google LLC.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu_work

#if !defined(_TRACE_GPU_WORK_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_WORK_H

#include <linux/tracepoint.h>

/*
 * The gpu events occur when workload is submitted or returned from GPU back to kmd.
 *
 * This event should be emitted whenever the kernel device driver notifies FW that
 * new tasks are available for GPU to execute.
 *
 * @gpu_id: This is the gpu id.
 *
 * @pid: Put 0 for global total, while positive pid for process total.
 *
 */
TRACE_EVENT(gpu_kick,

	TP_PROTO(uint32_t gpu_id, uint32_t pid, uint32_t type),

	TP_ARGS(gpu_id, pid, type),

	TP_STRUCT__entry(
		__field(uint32_t, gpu_id)
		__field(uint32_t, pid)
		__field(uint32_t, type)
	),

	TP_fast_assign(
		__entry->gpu_id = gpu_id;
		__entry->pid = pid;
		__entry->type = type;
	),

	TP_printk("gpu_id=%u pid=%u, type=%u",
		__entry->gpu_id,
		__entry->pid,
		__entry->type)
);

/*
 * The gpu events occur when workload is submitted or returned from GPU back to kmd.
 *
 * This event should be emitted whenever the kernel device driver wants to enqueue
 *  work to GPU but is unable to because queues to GPU are full.
 *
 * @gpu_id: This is the gpu id.
 *
 * @pid: Put 0 for global total, while positive pid for process total.
 *
 */
TRACE_EVENT(gpu_queue_full,

	TP_PROTO(uint32_t gpu_id, uint32_t pid),

	TP_ARGS(gpu_id, pid),

	TP_STRUCT__entry(
		__field(uint32_t, gpu_id)
		__field(uint32_t, pid)
	),

	TP_fast_assign(
		__entry->gpu_id = gpu_id;
		__entry->pid = pid;
	),

	TP_printk("gpu_id=%u pid=%u",
		__entry->gpu_id,
		__entry->pid)
);
#endif /* _TRACE_GPU_WORK_H */

/* This part must be outside protection */
#include <trace/define_trace.h>

