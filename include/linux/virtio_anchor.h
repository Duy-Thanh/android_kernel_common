/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_ANCHOR_H
#define _LINUX_VIRTIO_ANCHOR_H

#ifdef CONFIG_VIRTIO_ANCHOR
struct virtio_device;

/* The priority of CB_TYPE_VIRTIO_MSG is higher than CB_TYPE_XEN callback here */
enum callback_type {
	CB_NONE,
	CB_TYPE_XEN,
	CB_TYPE_VIRTIO_MSG,
};

bool virtio_require_restricted_mem_acc(struct virtio_device *dev);
extern bool (*virtio_check_mem_acc_cb)(struct virtio_device *dev);
extern enum callback_type virtio_check_mem_acc_cb_type;

static inline void virtio_set_mem_acc_cb_type(bool (*func)(struct virtio_device *), enum callback_type type)
{
	/* CB_TYPE_VIRTIO_MSG has a higher priority */
	if (type > virtio_check_mem_acc_cb_type) {
		virtio_check_mem_acc_cb = func;
		virtio_check_mem_acc_cb_type = type;
	}
}

static inline void virtio_set_mem_acc_cb(bool (*func)(struct virtio_device *))
{
	virtio_set_mem_acc_cb_type(func, CB_TYPE_XEN);
	virtio_check_mem_acc_cb_type = CB_TYPE_XEN;
}

#else
#define virtio_set_mem_acc_cb(func) do { } while (0)
#endif

#endif /* _LINUX_VIRTIO_ANCHOR_H */
