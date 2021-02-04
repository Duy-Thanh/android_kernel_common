/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 Google, Inc.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM trusty

#if !defined(_TRUSTY_IPC_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRUSTY_IPC_TRACE_H

#include <linux/tracepoint.h>
#include <uapi/linux/trusty/ipc.h>
#include <linux/trusty/trusty_ipc.h>

TRACE_EVENT(trusty_ipc_connect,
	TP_PROTO(struct tipc_chan *chan, const char *port),
	TP_ARGS(chan, port),
	TP_STRUCT__entry(
		__field(u32, chan)
		__string(port, port)
		__field(int, state)
	),
	TP_fast_assign(
		__entry->chan = chan ? chan->local : ~0U;
		__assign_str(port, port);
		__entry->state = chan ? chan->state: 0U;
	),
	TP_printk("chan=%u port=%s state=%d", __entry->chan, __get_str(port), __entry->state)
);

TRACE_EVENT(trusty_ipc_connect_end,
	TP_PROTO(struct tipc_chan *chan, int err),
	TP_ARGS(chan, err),
	TP_STRUCT__entry(
		__field(u32, chan)
		__field(int, err)
		__field(int, state)
	),
	TP_fast_assign(
		__entry->chan = chan ? chan->local : ~0U;
		__entry->err = err;
		__entry->state = chan ? chan->state: 0U;
	),
	TP_printk("chan=%u err=%d state=%d", __entry->chan, __entry->err, __entry->state)
);

TRACE_EVENT(trusty_ipc_handle_event,
	TP_PROTO(struct tipc_chan *chan, u32 event_id),
	TP_ARGS(chan, event_id),
	TP_STRUCT__entry(
		__field(u32, chan)
		__array(char, srv_name, MAX_SRV_NAME_LEN)
		__array(char, event, TIPC_CHAN_EVENT_NAME_MAXLEN)
	),
	TP_fast_assign(
		__entry->chan = chan ? chan->local : ~0U;
		memcpy(__entry->srv_name, chan ? chan->srv_name : "", MAX_SRV_NAME_LEN);
		memcpy(__entry->event, tipc_chan_event_name(event_id), 16);
	),
	TP_printk("chan=%u srv_name=%s event=%s", __entry->chan, __entry->srv_name, __entry->event)
);

TRACE_DEFINE_ENUM(TRUSTY_SHARE);
TRACE_DEFINE_ENUM(TRUSTY_LEND);
TRACE_DEFINE_ENUM(TRUSTY_SEND_SECURE);
TRACE_DEFINE_ENUM(TRUSTY_NO_SHM);

#define tipc_transfer_kind(kind) \
        __print_symbolic(kind, \
                { TRUSTY_SHARE, "TRUSTY_SHARE" }, \
                { TRUSTY_LEND, "TRUSTY_LEND" }, \
                { TRUSTY_SEND_SECURE, "TRUSTY_SEND_SECURE" }, \
                { TRUSTY_NO_SHM, "TRUSTY_NO_SHM" })

TRACE_EVENT(trusty_ipc_write,
	TP_PROTO(struct tipc_chan *chan,
		int len_or_err,
		struct tipc_msg_buf *txbuf,
		struct trusty_shm *shm),
	TP_ARGS(chan, len_or_err, txbuf, shm),
	TP_STRUCT__entry(
		__field(int, len_or_err)
		__field(u32, chan)
		__array(char, srv_name, MAX_SRV_NAME_LEN)
		__field(u64, buf_id)
		__field(size_t, shm_cnt)
		__field(int, kind_shm0)
		__field(int, kind_shm1)
	),
	TP_fast_assign(
		__entry->len_or_err = len_or_err;
		__entry->chan = chan ? chan->local : ~0U;
		memcpy(__entry->srv_name, chan ? chan->srv_name : "", MAX_SRV_NAME_LEN);
		__entry->buf_id = txbuf ? txbuf->buf_id : ~0ULL;
		__entry->shm_cnt = txbuf ? txbuf->shm_cnt : 0;
		__entry->kind_shm0 = (txbuf && shm && (txbuf->shm_cnt > 0)) ? shm[0].transfer : TRUSTY_NO_SHM;
		__entry->kind_shm1 = (txbuf && shm && (txbuf->shm_cnt > 1)) ? shm[1].transfer : TRUSTY_NO_SHM;
	),
	TP_printk("len_or_err=%d chan=%u srv_name=%s buf_id=0x%llx shm_cnt=%zu kind_shm0=%s kind_shm1=%s",
	__entry->len_or_err, __entry->chan, __entry->srv_name, __entry->buf_id,
	__entry->shm_cnt, tipc_transfer_kind(__entry->kind_shm0), tipc_transfer_kind(__entry->kind_shm1))
);

DECLARE_EVENT_CLASS(trusty_ipc_frd_class,
	TP_PROTO(struct tipc_chan *chan),
	TP_ARGS(chan),
	TP_STRUCT__entry(
		__field(u32, chan)
		__array(char, srv_name, MAX_SRV_NAME_LEN)
	),
	TP_fast_assign(
		__entry->chan = chan ? chan->local : ~0U;
		memcpy(__entry->srv_name, chan ? chan->srv_name : "", MAX_SRV_NAME_LEN);
	),
	TP_printk("chan=%u srv_name=%s", __entry->chan, __entry->srv_name)
);

#define DEFINE_TRUSTY_IPC_FRD_EVENT(name)	\
DEFINE_EVENT(trusty_ipc_frd_class, name,	\
	TP_PROTO(struct tipc_chan *chan),		\
	TP_ARGS(chan) \
);

DEFINE_TRUSTY_IPC_FRD_EVENT(trusty_ipc_read);

TRACE_EVENT(trusty_ipc_read_end,
	TP_PROTO(struct tipc_chan *chan,
		int len_or_err,
		struct tipc_msg_buf *rxbuf),
	TP_ARGS(chan, len_or_err, rxbuf),
	TP_STRUCT__entry(
		__field(int, len_or_err)
		__field(u32, chan)
		__array(char, srv_name, MAX_SRV_NAME_LEN)
		__field(u64, buf_id)
		__field(size_t, shm_cnt)
	),
	TP_fast_assign(
		__entry->len_or_err = len_or_err;
		__entry->chan = chan ? chan->local : ~0U;
		memcpy(__entry->srv_name, chan ? chan->srv_name : "", MAX_SRV_NAME_LEN);
		__entry->buf_id = rxbuf ? rxbuf->buf_id : ~0ULL;
		__entry->shm_cnt = rxbuf ? rxbuf->shm_cnt : 0;
	),
	TP_printk("len_or_err=%d chan=%u srv_name=%s buf_id=0x%llx shm_cnt=%zu",
	__entry->len_or_err, __entry->chan, __entry->srv_name, __entry->buf_id, __entry->shm_cnt)
);

TRACE_EVENT(trusty_ipc_poll,
	TP_PROTO(struct tipc_chan *chan,
		unsigned int poll_mask),
	TP_ARGS(chan, poll_mask),
	TP_STRUCT__entry(
		__field(unsigned int, poll_mask)
		__field(u32, chan)
		__array(char, srv_name, MAX_SRV_NAME_LEN)
	),
	TP_fast_assign(
		__entry->poll_mask = poll_mask;
		__entry->chan = chan ? chan->local : ~0U;
		memcpy(__entry->srv_name, chan ? chan->srv_name : "", MAX_SRV_NAME_LEN);
	),
	TP_printk("poll_mask=%u chan=%u srv_name=%s",
	__entry->poll_mask, __entry->chan, __entry->srv_name)
);

/*
 * tracepoint when a message buffer is received from trusty
 * and is awaiting for its HAL consumer to read it
 */
TRACE_EVENT(trusty_ipc_rx,
	TP_PROTO(struct tipc_chan *chan, struct tipc_msg_buf *rxbuf),
	TP_ARGS(chan, rxbuf),
	TP_STRUCT__entry(
		__field(u32, chan)
		__array(char, srv_name, MAX_SRV_NAME_LEN)
		__field(u64, buf_id)
	),
	TP_fast_assign(
		__entry->chan = chan ? chan->local : ~0U;
		memcpy(__entry->srv_name, chan ? chan->srv_name : "", MAX_SRV_NAME_LEN);
		__entry->buf_id = rxbuf->buf_id;
	),
	TP_printk("chan=%u srv_name=%s buf_id=0x%llx", __entry->chan, __entry->srv_name, __entry->buf_id)
);
#endif /* _TRUSTY_IPC_TRACE_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH .
#define TRACE_INCLUDE_FILE trusty-ipc-trace
#include <trace/define_trace.h>
