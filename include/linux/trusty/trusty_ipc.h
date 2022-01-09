/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015 Google, Inc.
 */
#ifndef __LINUX_TRUSTY_TRUSTY_IPC_H
#define __LINUX_TRUSTY_TRUSTY_IPC_H

#include <linux/list.h>
#include <linux/scatterlist.h>
#include <linux/trusty/trusty.h>
#include <linux/types.h>

/**
 * DOC: Trusty IPC Linux Driver
 *
 * Typical usage of the :ref:`trusty_ipc` driver consists of a caller creating a
 * :type:`tipc_chan` opaque object by using the :func:`tipc_create_channel`
 * function and then using the :func:`tipc_chan_connect` call to initiate a
 * connection to the Trusty IPC service running on the secure side.
 * The connection to the remote side can be terminated by calling
 * :func:`tipc_chan_shutdown` followed by :func:`tipc_chan_destroy` to clean up
 * resources.
 *
 * Upon receiving a notification (through the :func:`handle_event` callback)
 * that a connection has been successfully established, a caller does the
 * following:
 *
 * *   Obtains a message buffer using the :func:`tipc_chan_get_txbuf_timeout`
 *     call
 * *   Composes a message, and
 * *   Queues the message using the :func:`tipc_chan_queue_msg` method for
 *     delivery to a Trusty service (on the secure side), to which the channel
 *     is connected
 *
 * After queueing is successful, the caller should forget the message buffer
 * because the message buffer eventually returns to the free buffer pool after
 * processing by the remote side (for reuse later, for other messages). The user
 * only needs to call :func:`tipc_chan_put_txbuf` if it fails to queue such
 * buffer or it is not required anymore.
 *
 * An API user receives messages from the remote side by handling a
 * :func:`handle_msg` notification callback (which is called in the context of
 * the trusty-ipc `rx` workqueue) that provides a pointer to an `rx` buffer
 * containing an incoming message to be handled.
 *
 * It is expected that the :func:`handle_msg` callback implementation will
 * return a pointer to a valid :type:`struct tipc_msg_buf`. It can be the same
 * as the incoming message buffer if it is handled locally and not required
 * anymore. Alternatively, it can be a new buffer obtained by a
 * :func:`tipc_chan_get_rxbuf` call if the incoming buffer is queued for further
 * processing. A detached `rx` buffer must be tracked and eventually released
 * using a :func:`tipc_chan_put_rxbuf` call when it is no longer needed.
 *
 *
 */

struct tipc_chan;

/**
 * struct tipc_msg_buf - tipc message buffer metadata
 */
struct tipc_msg_buf {
	void *buf_va;
	struct scatterlist sg;
	trusty_shared_mem_id_t buf_id;
	size_t buf_sz;
	size_t wpos;
	size_t rpos;
	size_t shm_cnt;
	struct list_head node;
};

/**
 * enum tipc_chan_event - tipc channel status
 */
enum tipc_chan_event {
	TIPC_CHANNEL_CONNECTED = 1,
	TIPC_CHANNEL_DISCONNECTED,
	TIPC_CHANNEL_SHUTDOWN,
};

/**
 * struct tipc_chan_ops - tipc channel data structure
 * @handle_event: handle_event callback is invoked to notify a
 *                caller about a channel state change.
 *                @cb_arg: Pointer to data passed to a
 *                        :func:`tipc_create_channel() call
 *                @event:  An event that can be one of the following values:
 *                         * %TIPC_CHANNEL_CONNECTED - indicates a successful
 *                           connection to the remote side
 *                         * %TIPC_CHANNEL_DISCONNECTED - indicates the
 *                           remote side denied the new connection request
 *                           or requested disconnection for the previously
 *                           connected channel
 *                         * %TIPC_CHANNEL_SHUTDOWN - indicates the remote side
 *                           is shutting down, permanently terminating all
 *                           connections
 * @handle_msg:   handle_msg callback is invoked to provide notification that
 *                a new message has been received over a specified channel:
 *                @cb_arg: Pointer to data passed to the
 *                         :func:`tipc_create_channel` call
 *                @mb:     Pointer to a `struct tipc_msg_buf` describing
 *                         an incoming message
 *                Return: The callback implementation is expected to return
 *                a pointer to a :type:`struct tipc_msg_buf` that can be the
 *                same pointer received as an `mb` parameter if the message
 *                is handled locally and is not required anymore (or
 *                it can be a new buffer obtained by the
 *                :func:`tipc_chan_get_rxbuf` call)
 * @handle_release: handle_release callback
 */
struct tipc_chan_ops {
	void (*handle_event)(void *cb_arg, int event);
	struct tipc_msg_buf *(*handle_msg)(void *cb_arg,
					   struct tipc_msg_buf *mb);
	void (*handle_release)(void *cb_arg);
};

/**
 * tipc_create_channel() - Creates and configures an instance of a Trusty IPC
 * channel for a particular trusty-ipc device.
 * @dev:    Pointer to the trusty-ipc for which the device channel is created
 * @ops:    Pointer to a `struct tipc_chan_ops`, with caller-specific
 *          callbacks filled in
 * @cb_arg: Pointer to data that will be passed to `tipc_chan_ops`
 *          callbacks
 *
 * In general, a caller must provide :type:`tipc_chan_ops.handle_event` and
 * :type:`tipc_chan_ops.handle_msg` callbacks that are asynchronously
 * invoked when the corresponding activity is occurring.
 *
 * Return: Pointer to a newly-created instance of :type:`struct tipc_chan` on
 * success, `ERR_PTR(err)` otherwise
 *
 */
struct tipc_chan *tipc_create_channel(struct device *dev,
				      const struct tipc_chan_ops *ops,
				      void *cb_arg);

/**
 * tipc_chan_connect() - Initiates a connection to the specified
 * Trusty IPC service.
 *
 * @chan: Pointer to a channel returned by the `tipc_create_chan()` call
 * @port: Pointer to a string containing the service name to which to
 *        connect
 *
 * The caller is notified when a connection is established by receiving a
 * :type:`tipc_chan_ops.handle_event` callback.
 *
 * Return: 0 on success, a negative error otherwise
 *
 */
int tipc_chan_connect(struct tipc_chan *chan, const char *port);

/**
 * tipc_chan_queue_msg() - Queues a message to be sent over the specified
 * Trusty IPC channels.
 * @chan: Pointer to the channel to which to queue the message
 * @mb:   Pointer to the message to queue (obtained by a
 *        :func:`tipc_chan_get_txbuf_timeout` call)
 *
 * Return: 0 on success, a negative error otherwise
 */
int tipc_chan_queue_msg(struct tipc_chan *chan, struct tipc_msg_buf *mb);

/**
 * tipc_chan_shutdown() - Terminates a connection to the Trusty IPC service
 * previously initiated by a :func:`tipc_chan_connect` call.
 * @chan: Pointer to a channel returned by a `tipc_create_chan()` call
 *
 * Return: 0 on success, a negative error otherwise
 */
int tipc_chan_shutdown(struct tipc_chan *chan);

/**
 * tipc_chan_destroy() - Destroys a specified Trusty IPC channel.
 * @chan: Pointer to a channel returned by the :func:`tipc_create_chan` call
 *
 * Return: 0 on success, a negative error otherwise
 */
void tipc_chan_destroy(struct tipc_chan *chan);

/**
 * tipc_chan_get_rxbuf() - Obtains a new message buffer that can be used
 * to receive messages over the specified channel.
 * @chan: Pointer to a channel to which this message buffer belongs
 *
 * Return: A valid message buffer on success, `ERR_PTR(err)` on error
 *
 */
struct tipc_msg_buf *tipc_chan_get_rxbuf(struct tipc_chan *chan);

/**
 * tipc_chan_put_rxbuf() - Releases a specified message buffer previously
 * obtained by a :func:`tipc_chan_get_rxbuf` call.
 * @chan: Pointer to a channel to which this message buffer belongs
 * @mb:   Pointer to a message buffer to release
 *
 */
void tipc_chan_put_rxbuf(struct tipc_chan *chan, struct tipc_msg_buf *mb);

/**
 * tipc_chan_get_txbuf_timeout() - Obtains a message buffer that can be used
 * to send data over a specified channel.
 * @chan:    Pointer to the channel to which to queue a message
 * @timeout: Maximum timeout to wait until the `tx` buffer becomes available
 *
 * If the buffer is not immediately available the caller may be blocked for the
 * specified timeout (in milliseconds).
 *
 * Return: A valid message buffer on success, `ERR_PTR(err)` on error
 *
 */
struct tipc_msg_buf *tipc_chan_get_txbuf_timeout(struct tipc_chan *chan,
						 long timeout);
/**
 * tipc_chan_put_txbuf() - Releases the specified `Tx` message buffer
 * previously obtained by a :func:`tipc_chan_get_txbuf_timeout` call.
 * @chan: Pointer to the channel to which this message buffer belongs
 * @mb:   Pointer to the message buffer to release
 */
void tipc_chan_put_txbuf(struct tipc_chan *chan, struct tipc_msg_buf *mb);

static inline size_t mb_avail_space(struct tipc_msg_buf *mb)
{
	return mb->buf_sz - mb->wpos;
}

static inline size_t mb_avail_data(struct tipc_msg_buf *mb)
{
	return mb->wpos - mb->rpos;
}

static inline void *mb_put_data(struct tipc_msg_buf *mb, size_t len)
{
	void *pos = (u8 *)mb->buf_va + mb->wpos;

	BUG_ON(mb->wpos + len > mb->buf_sz);
	mb->wpos += len;
	return pos;
}

static inline void *mb_get_data(struct tipc_msg_buf *mb, size_t len)
{
	void *pos = (u8 *)mb->buf_va + mb->rpos;

	BUG_ON(mb->rpos + len > mb->wpos);
	mb->rpos += len;
	return pos;
}

#endif /* __LINUX_TRUSTY_TRUSTY_IPC_H */
