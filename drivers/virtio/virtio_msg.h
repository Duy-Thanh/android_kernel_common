/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Virtio message transport header.
 *
 * Copyright (c) 2024 Advanced Micro Devices, Inc.
 * Written by Edgar E. Iglesias <edgar.iglesias@amd.com>
 *
 * Copyright (C) 2024 Google LLC and Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 *
 * The Virtio message transport allows virtio devices to be used over a virtual
 * virtio-msg channel. The channel interface is meant to be implemented using
 * the architecture specific hardware-assisted fast path, like ARM Firmware
 * Framework (FFA).
 */

#ifndef _DRIVERS_VIRTIO_VIRTIO_MSG_H
#define _DRIVERS_VIRTIO_VIRTIO_MSG_H

#include <linux/list.h>
#include <linux/pm.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <uapi/linux/virtio_msg.h>

struct device;
struct virtio_msg_device;

/**
 * struct virtio_msg_ops - operations for configuring a virtio message device
 * @send: Transfer a message.
 * @remove: Unregister the device.
 */
struct virtio_msg_ops {
	int (*send)(struct virtio_msg_device *vmdev, struct virtio_msg *request,
		    struct virtio_msg *response);
	const char *(*bus_name)(struct virtio_msg_device *vmdev);
	void (*synchronize_cbs)(struct virtio_msg_device *vmdev);
	void (*release)(struct virtio_msg_device *vmdev);
	int (*prepare_vqs)(struct virtio_msg_device *vmdev);
	void (*release_vqs)(struct virtio_msg_device *vmdev);
};

/**
 * struct virtio_msg_async - async transfer infrastructure
 */
struct virtio_msg_async {
	struct completion completion;
};

/**
 * struct virtio_msg_device - representation of a device using virtio message
 */
struct virtio_msg_device {
	struct virtio_device vdev;
	struct virtio_msg_ops *ops;
	struct virtio_msg_async async;
	void *priv;
	u16 dev_id;

	/* a list of queues so we can dispatch IRQs */
	spinlock_t lock;
	struct list_head virtqueues;
};

struct virtio_msg_vq {
	struct virtqueue *vq;
	struct list_head node;
};

#ifdef CONFIG_PM_SLEEP
static inline int virtio_msg_freeze(struct virtio_msg_device *vmdev)
{
	return virtio_device_freeze(&vmdev->vdev);
}

static inline int virtio_msg_restore(struct virtio_msg_device *vmdev)
{
	return virtio_device_restore(&vmdev->vdev);
}
#endif

int virtio_msg_receive(struct virtio_msg_device *vmdev, struct virtio_msg *msg);
int virtio_msg_register(struct virtio_msg_device *vmdev);
void virtio_msg_unregister(struct virtio_msg_device *vmdev);

void virtio_msg_prepare(struct virtio_msg *msg, bool bus, u8 msg_id, u16 dev_id);

static inline void virtio_msg_async_init(struct virtio_msg_async *async)
{
	init_completion(&async->completion);
}

static inline int virtio_msg_async_wait(struct virtio_msg_async *async,
		struct device *dev, unsigned long timeout)
{
	int ret;

	if (timeout)
		ret = wait_for_completion_interruptible_timeout(&async->completion, timeout);
	else
		ret = wait_for_completion_interruptible(&async->completion);

	if (ret < 0) {
		dev_err(dev, "Interrupted while waiting for response: %d\n", ret);
	} else if (timeout && !ret) {
		dev_err(dev, "Timed out waiting for response\n");
		ret = -ETIMEDOUT;
	} else {
		ret = 0;
	}

	return ret;
}

static inline void virtio_msg_async_wait_nosleep(struct virtio_msg_async *async)
{
	while (!try_wait_for_completion(&async->completion))
		cpu_relax();
}

static inline void virtio_msg_async_complete(struct virtio_msg_async *async)
{
	complete(&async->completion);
}

#if IS_REACHABLE(CONFIG_VIRTIO_MSG_FFA)
int vmsg_ffa_bus_area_share(struct device *dev, void *vaddr, size_t n_pages,
			    dma_addr_t *dma_handle);
int vmsg_ffa_bus_area_unshare(struct device *dev, dma_addr_t *dma_handle,
			      size_t num_pages);
#endif

#ifdef CONFIG_VIRTIO_MSG_FFA_DMA_OPS
extern const struct dma_map_ops virtio_msg_ffa_dma_ops;
int virtio_msg_ffa_dma_init(void);
#else
static inline int virtio_msg_ffa_dma_init(void) {
	return 0;
}
#endif

#endif /* _DRIVERS_VIRTIO_VIRTIO_MSG_H */
