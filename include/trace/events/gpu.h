/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM gpu

#if !defined(_TRACE_GPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_GPU_H

#include <linux/tracepoint.h>

/*
 * The gpu_sched_enqueue event indicates that commands from the userland has
 * been queued up to the GPU kernel mode driver.
 *
 * This event should be traced in the kernel thread that handles the enqueue of
 * those commands.
 *
 * @pid: The id of the userland process that submits the commands to the kernel.
 *
 * @ctx_id: It identifies the GPU context in which the commands being queued is
 * to be run.
 *
 * @ctx_prio: The context's priority for commands submission to the GPU.
 *
 * @submission_id: This is the id corresponding to the userland application's
 * submission of the commands. This is used to group together the sub-tasks
 * which the user-mode or kernel-mode driver can potentially split the global
 * submission up into.
 *
 * @msg: This is an optional field to pass additional debug information.
 *
 */
TRACE_EVENT(gpu_sched_enqueue,
	TP_PROTO(
		uint32_t pid,
		uint32_t ctx_id,
		uint32_t ctx_prio,
		uint32_t submission_id,
		const char* msg
	),
	TP_ARGS(
		pid,
		ctx_id,
		ctx_prio,
		submission_id,
		msg
	),
	TP_STRUCT__entry(
		__field(uint32_t, pid)
		__field(uint32_t, ctx_id)
		__field(uint32_t, ctx_prio)
		__field(uint32_t, submission_id)
		__string(msg, msg)
	),
	TP_fast_assign(
		__entry->pid = pid;
		__entry->ctx_id = ctx_id;
		__entry->ctx_prio = ctx_prio;
		__entry->submission_id = submission_id;
		__assign_str(msg, msg);
	),
	TP_printk(
		"pid=%u "
		"ctx_id=%u "
		"ctx_prio=%u "
		"submission_id=%u "
		"msg=%s",
		__entry->pid,
		__entry->ctx_id,
		__entry->ctx_prio,
		__entry->submission_id,
		__get_str(msg)
	)
);

/*
 * The gpu_sched_submit event indicates that the commands from the GPU context
 * have been submitted to the GPU hardware for execution or to be executed.
 *
 * This event should be traced in the kernel thread that dispatches the commands
 * from the GPU context to the GPU hardware queues.
 *
 * @pid: The id of the userland process that submits the commands to the kernel.
 *
 * @ctx_id: It identifies the GPU context in which the commands being queued is
 * to be run.
 *
 * @ctx_prio: The context's priority for commands submission to the GPU.
 *
 * @submission_id: This is the id corresponding to the userland application's
 * submission of the commands. This is used to group together the sub-tasks
 * which the user-mode or kernel-mode driver can potentially split the global
 * submission up into.
 *
 * @hw_queue_id: This identifies which GPU hardware queue the commands have been
 * submitted to.
 *
 * @msg: This is an optional field to pass additional debug information.
 *
 */
TRACE_EVENT(gpu_sched_submit,
	TP_PROTO(
		uint32_t pid,
		uint32_t ctx_id,
		uint32_t ctx_prio,
		uint32_t submission_id,
		uint32_t hw_queue_id,
		const char* msg
	),
	TP_ARGS(
		pid,
		ctx_id,
		ctx_prio,
		submission_id,
		hw_queue_id,
		msg
	),
	TP_STRUCT__entry(
		__field(uint32_t, pid)
		__field(uint32_t, ctx_id)
		__field(uint32_t, ctx_prio)
		__field(uint32_t, submission_id)
		__field(uint32_t, hw_queue_id)
		__string(msg, msg)
	),
	TP_fast_assign(
		__entry->pid = pid;
		__entry->ctx_id = ctx_id;
		__entry->ctx_prio = ctx_prio;
		__entry->submission_id = submission_id;
		__entry->hw_queue_id = hw_queue_id;
		__assign_str(msg, msg);
	),
	TP_printk(
		"pid=%u "
		"ctx_id=%u "
		"ctx_prio=%u "
		"submission_id=%u "
		"hw_queue_id=%u "
		"msg=%s",
		__entry->pid,
		__entry->ctx_id,
		__entry->ctx_prio,
		__entry->submission_id,
		__entry->hw_queue_id,
		__get_str(msg)
	)
);

/*
 * The gpu_sched_complete event indicates that commands executed on the GPU
 * hardware have finished, or get preempted, or for any reason stopped.
 *
 * This event should be traced in the kernel thread that handles the completion
 * of the commands execution. It could be in the interrupt handler or just
 * random kernel thread that cleans up the completed commands as long as the
 * timestamps in this events are accurate.
 *
 * @pid: The id of the userland process that submits the commands to the kernel.
 *
 * @ctx_id: It identifies the GPU context in which the commands being queued is
 * to be run.
 *
 * @ctx_prio: The context's priority for commands submission to the GPU.
 *
 * @submission_id: This is the id corresponding to the userland application's
 * submission of the commands. This is used to group together the sub-tasks
 * which the user-mode or kernel-mode driver can potentially split the global
 * submission up into.
 *
 * @gpu_start_time: This is the timestamp in nanoseconds for when the GPU
 * hardware starts executing the submitted commands.
 *
 * @gpu_end_time: This is the timestamp in nanoseconds for when the GPU hardware
 * ends executing the submitted commands.
 *
 * @trace_emit_time: This is the timestamp in nanoseconds for when this ftrace
 * event gets emitted. Need to be as close as possible to precisely reproduce
 * the GPU render time slice.
 *
 * @msg: This is an optional field to pass additional debug information. For
 * this event, the reason for this completion should be placed in the msg.
 *
 */
TRACE_EVENT(gpu_sched_complete,
	TP_PROTO(
		uint32_t pid,
		uint32_t ctx_id,
		uint32_t ctx_prio,
		uint32_t submission_id,
		uint64_t gpu_start_time,
		uint64_t gpu_end_time,
		uint64_t trace_emit_time,
		const char* msg
	),
	TP_ARGS(
		pid,
		ctx_id,
		ctx_prio,
		submission_id,
		gpu_start_time,
		gpu_end_time,
		trace_emit_time,
		msg
	),
	TP_STRUCT__entry(
		__field(uint32_t, pid)
		__field(uint32_t, ctx_id)
		__field(uint32_t, ctx_prio)
		__field(uint32_t, submission_id)
		__field(uint64_t, gpu_start_time)
		__field(uint64_t, gpu_end_time)
		__field(uint64_t, trace_emit_time)
		__string(msg, msg)
	),
	TP_fast_assign(
		__entry->pid = pid;
		__entry->ctx_id = ctx_id;
		__entry->ctx_prio = ctx_prio;
		__entry->submission_id = submission_id;
		__entry->gpu_start_time = gpu_start_time;
		__entry->gpu_end_time = gpu_end_time;
		__entry->trace_emit_time = trace_emit_time;
		__assign_str(msg, msg);
	),
	TP_printk(
		"pid=%u "
		"ctx_id=%u "
		"ctx_prio=%u "
		"submission_id=%u "
		"gpu_start_time=%llu "
		"gpu_end_time=%llu "
		"trace_emit_time=%llu "
		"msg=%s",
		__entry->pid,
		__entry->ctx_id,
		__entry->ctx_prio,
		__entry->submission_id,
		__entry->gpu_start_time,
		__entry->gpu_end_time,
		__entry->trace_emit_time,
		__get_str(msg)
	)
);

/*
 * The gpu_frequency event indicates frequency changes on the GPU clock domain.
 *
 * This event should be traced whenever there's a frequency change on the GPU
 * clock domain. At the start of tracing, this event should be additionally
 * emitted to record the initial GPU clock for the tracing period. Whenever GPU
 * goes into idle state, this event must be emitted for accurate tracing.
 *
 * @state: The new GPU frequency.
 * @gpu_id: It identifies different GPU clock domains.
 *
 */
TRACE_EVENT(gpu_frequency,
	TP_PROTO(
		uint32_t state,
		uint32_t gpu_id
	),
	TP_ARGS(
		state,
		gpu_id
	),
	TP_STRUCT__entry(
		__field(uint32_t, state)
		__field(uint32_t, gpu_id)
	),
	TP_fast_assign(
		__entry->state = state;
		__entry->gpu_id = gpu_id;
	),
	TP_printk(
		"state=%u "
		"gpu_id=%u",
		__entry->state,
		__entry->gpu_id
	)
);

#endif /* _TRACE_GPU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
