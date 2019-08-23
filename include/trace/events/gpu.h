/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu

#if !defined(_TRACE_GPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_H

#include <linux/tracepoint.h>

TRACE_EVENT(gpu_sched_enqueue,
	TP_PROTO(
		uint32_t pid,
		uint32_t ctx_id,
		uint32_t ctx_prio,
		uint32_t job_id,
		uint32_t submission_id,
		const char* msg
	),
	TP_ARGS(
		pid,
		ctx_id,
		ctx_prio,
		job_id,
		submission_id,
		msg
	),
	TP_STRUCT__entry(
		__field(uint32_t, pid)
		__field(uint32_t, ctx_id)
		__field(uint32_t, ctx_prio)
		__field(uint32_t, job_id)
		__field(uint32_t, submission_id)
		__string(msg, msg)
	),
	TP_fast_assign(
		__entry->pid = pid;
		__entry->ctx_id = ctx_id;
		__entry->ctx_prio = ctx_prio;
		__entry->job_id = job_id;
		__entry->submission_id = submission_id;
		__assign_str(msg, msg);
	),
	TP_printk(
		"pid=%u "
		"ctx_id=%u "
		"ctx_prio=%u "
		"job_id=%u "
		"submission_id=%u "
		"msg=%s",
		__entry->pid,
		__entry->ctx_id,
		__entry->ctx_prio,
		__entry->job_id,
		__entry->submission_id,
		__get_str(msg)
	)
);

TRACE_EVENT(gpu_sched_submit,
	TP_PROTO(
		uint32_t pid,
		uint32_t ctx_id,
		uint32_t ctx_prio,
		uint32_t job_id,
		uint32_t submission_id,
		uint32_t hw_queue_id,
		const char* msg
	),
	TP_ARGS(
		pid,
		ctx_id,
		ctx_prio,
		job_id,
		submission_id,
		job_id,
		hw_queue_id,
		msg
	),
	TP_STRUCT__entry(
		__field(uint32_t, pid)
		__field(uint32_t, ctx_id)
		__field(uint32_t, ctx_prio)
		__field(uint32_t, job_id)
		__field(uint32_t, submission_id)
		__field(uint32_t, hw_queue_id)
		__string(msg, msg)
	),
	TP_fast_assign(
		__entry->pid = pid;
		__entry->ctx_id = ctx_id;
		__entry->ctx_prio = ctx_prio;
		__entry->job_id = job_id;
		__entry->submission_id = submission_id;
		__entry->hw_queue_id = hw_queue_id;
		__assign_str(msg, msg);
	),
	TP_printk(
		"pid=%u "
		"ctx_id=%u "
		"ctx_prio=%u "
		"job_id=%u "
		"submission_id=%u "
		"hw_queue_id=%u "
		"msg=%s",
		__entry->pid,
		__entry->ctx_id,
		__entry->job_id,
		__entry->submission_id,
		__entry->hw_queue_id,
		__get_str(msg)
	)
);

TRACE_EVENT(gpu_sched_complete,
	TP_PROTO(
		uint32_t pid,
		uint32_t ctx_id,
		uint32_t ctx_prio,
		uint32_t job_id,
		uint32_t submission_id,
		uint64_t gpu_start_ts,
		uint64_t gpu_end_ts,
		uint64_t emit_ts,
		const char* msg
	),
	TP_ARGS(
		pid,
		ctx_id,
		ctx_prio,
		job_id,
		submission_id,
		gpu_start_ts,
		gpu_end_ts,
		emit_ts,
		msg
	),
	TP_STRUCT__entry(
		__field(uint32_t, pid)
		__field(uint32_t, ctx_id)
		__field(uint32_t, ctx_prio)
		__field(uint32_t, job_id)
		__field(uint32_t, submission_id)
		__field(uint64_t, gpu_start_ts)
		__field(uint64_t, gpu_end_ts)
		__field(uint64_t, emit_ts)
		__string(msg, msg)
	),
	TP_fast_assign(
		__entry->pid = pid;
		__entry->ctx_id = ctx_id;
		__entry->ctx_prio = ctx_prio;
		__entry->job_id = job_id;
		__entry->submission_id = submission_id;
		__entry->gpu_start_ts = gpu_start_ts;
		__entry->gpu_end_ts = gpu_end_ts;
		__entry->emit_ts = emit_ts;
		__assign_str(msg, msg);
	),
	TP_printk(
		"pid=%u "
		"ctx_id=%u "
		"ctx_prio=%u "
		"job_id=%u "
		"submission_id=%u "
		"gpu_start_ts=%"PRIu64" "
		"gpu_end_ts=%"PRIu64" "
		"emit_ts=%"PRIu64" "
		"msg=%s",
		__entry->pid,
		__entry->ctx_id,
		__entry->ctx_prio,
		__entry->job_id,
		__entry->submission_id,
		__entry->gpu_start_ts,
		__entry->gpu_end_ts,
		__entry->emit_ts,
		__get_str(msg)
	)
);

#endif /* _TRACE_GPU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
