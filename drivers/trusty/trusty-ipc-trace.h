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
	),
	TP_fast_assign(
		__entry->chan = chan ? chan->local : ~0U;
		__assign_str(port, port);
	),
	TP_printk("chan=%u port=%s", __entry->chan, __get_str(port))
);

TRACE_EVENT(trusty_ipc_connect_end,
	TP_PROTO(struct tipc_chan *chan, int err),
	TP_ARGS(chan, err),
	TP_STRUCT__entry(
		__field(u32, chan)
		__field(int, err)
	),
	TP_fast_assign(
		__entry->chan = chan ? chan->local : ~0U;
		__entry->err = err;
	),
	TP_printk("chan=%u err=%d", __entry->chan, __entry->err)
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

TRACE_EVENT(trusty_ipc_write,
	TP_PROTO(struct tipc_chan *chan, size_t shm_cnt),
	TP_ARGS(chan, shm_cnt),
	TP_STRUCT__entry(
		__field(u32, chan)
		__array(char, srv_name, MAX_SRV_NAME_LEN)
		__field(size_t, shm_cnt)
	),
	TP_fast_assign(
		__entry->chan = chan ? chan->local : ~0U;
		memcpy(__entry->srv_name, chan ? chan->srv_name : "", MAX_SRV_NAME_LEN);
		__entry->shm_cnt = (size_t)shm_cnt;
	),
	TP_printk("chan=%u srv_name=%s shm_cnt=%zu", __entry->chan, __entry->srv_name, __entry->shm_cnt)
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
DEFINE_TRUSTY_IPC_FRD_EVENT(trusty_ipc_poll);

TRACE_EVENT(trusty_ipc_write_end,
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
		__array(char, kind_shm0, TIPC_TRANSFER_KIND_NAME_MAXLEN)
		__array(char, kind_shm1, TIPC_TRANSFER_KIND_NAME_MAXLEN)
	),
	TP_fast_assign(
		__entry->len_or_err = len_or_err;
		__entry->chan = chan ? chan->local : ~0U;
		memcpy(__entry->srv_name, chan ? chan->srv_name : "", MAX_SRV_NAME_LEN);
		__entry->buf_id = txbuf ? txbuf->buf_id : ~0ULL;
		__entry->shm_cnt = txbuf ? txbuf->shm_cnt : 0;
		memcpy(__entry->kind_shm0, tipc_transfer_kind_name(
			(txbuf && shm && (txbuf->shm_cnt > 0)) ? shm[0].transfer : TRUSTY_NO_SHM),
			TIPC_TRANSFER_KIND_NAME_MAXLEN);
		memcpy(__entry->kind_shm1, tipc_transfer_kind_name(
			(txbuf && shm && (txbuf->shm_cnt > 1)) ? shm[1].transfer: TRUSTY_NO_SHM),
			TIPC_TRANSFER_KIND_NAME_MAXLEN);
	),
	TP_printk("len_or_err=%d chan=%u srv_name=%s buf_id=0x%llx shm_cnt=%zu kind_shm0=%s kind_shm1=%s",
	__entry->len_or_err, __entry->chan, __entry->srv_name, __entry->buf_id,
	__entry->shm_cnt, __entry->kind_shm0, __entry->kind_shm1)
);

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

TRACE_EVENT(trusty_ipc_poll_end,
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
 * tracepoint when a message buffer tx to trusty completes
 * and is awaiting for its TA consumer to process it
 */
TRACE_EVENT(trusty_ipc_tx,
	TP_PROTO(struct tipc_msg_buf *txbuf, unsigned int free_cnt),
	TP_ARGS(txbuf, free_cnt),
	TP_STRUCT__entry(
		__field(u64, buf_id)
		__field(unsigned int, free_cnt)
	),
	TP_fast_assign(
		__entry->buf_id = txbuf->buf_id;
		__entry->free_cnt = free_cnt;
	),
	TP_printk("buf_id=0x%llx, free_cnt=%u", __entry->buf_id,
		  __entry->free_cnt)
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
