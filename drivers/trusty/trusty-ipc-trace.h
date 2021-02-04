/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM trusty

#if !defined(_TRUSTY_IPC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRUSTY_IPC_TRACE_H

#include <linux/tracepoint.h>

TRACE_EVENT(trusty_ipc_connect,
	TP_PROTO(u32 chan, const char *port),
	TP_ARGS(chan, port),
	TP_STRUCT__entry(
		__field(u32, chan)
		__string(port, port)
	),
	TP_fast_assign(
		__entry->chan = chan;
		__assign_str(port, port);
	),
	TP_printk("chan=%u, port=%s", __entry->chan, __get_str(port))
);

TRACE_EVENT(trusty_ipc_handle_event,
	TP_PROTO(int event, u32 chan),
	TP_ARGS(event, chan),
	TP_STRUCT__entry(
		__field(int, event)
		__field(u32, chan)
	),
	TP_fast_assign(
		__entry->event = event;
		__entry->chan = chan;
	),
	TP_printk("event=%d, chan=%u", __entry->event, __entry->chan)
);

DECLARE_EVENT_CLASS(trusty_ipc_msg_class,
	TP_PROTO(u32 chan, u64 buf, size_t size, size_t shm_cnt),
	TP_ARGS(chan, buf, size, shm_cnt),
	TP_STRUCT__entry(
		__field(u32, chan)
		__field(u64, buf)
		__field(size_t, size)
		__field(size_t, shm_cnt)
	),
	TP_fast_assign(
		__entry->chan = chan;
		__entry->buf = buf;
		__entry->size = size;
		__entry->shm_cnt = shm_cnt;
	),
	TP_printk("chan=%u, buf_id=0x%llx, size=%zu, shm_cnt=%zu",
		  __entry->chan, __entry->buf, __entry->size, __entry->shm_cnt)
);

#define DEFINE_TRUSTY_IPC_MSG_EVENT(name)	\
DEFINE_EVENT(trusty_ipc_msg_class, name,	\
	TP_PROTO(u32 chan, u64 buf, size_t size, size_t shm_cnt), \
	TP_ARGS(chan, buf, size, shm_cnt))

DEFINE_TRUSTY_IPC_MSG_EVENT(trusty_ipc_rx);
DEFINE_TRUSTY_IPC_MSG_EVENT(trusty_ipc_tx);

TRACE_EVENT(trusty_ipc_tx_buf_done,
	TP_PROTO(u64 buf_id, unsigned int free_cnt),
	TP_ARGS(buf_id, free_cnt),
	TP_STRUCT__entry(
		__field(u64, buf_id)
		__field(unsigned int, free_cnt)
	),
	TP_fast_assign(
		__entry->buf_id = buf_id;
		__entry->free_cnt = free_cnt;
	),
	TP_printk("buf_id=0x%llx, free_cnt=%u", __entry->buf_id,
		  __entry->free_cnt)
);

#endif /* _TRUSTY_IPC_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trusty-ipc-trace
#include <trace/define_trace.h>
