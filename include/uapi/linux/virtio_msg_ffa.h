/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/*
 * Virtio message FFA (Arm Firmware Framework) transport header.
 *
 * Copyright (C) 2024 Google LLC and Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 */

#ifndef _LINUX_VIRTIO_MSG_FFA_H
#define _LINUX_VIRTIO_MSG_FFA_H

#include <linux/types.h>

/* Message types */
#define VIRTIO_MSG_FFA_ERROR				0x00
#define VIRTIO_MSG_FFA_ACTIVATE				0x01
#define VIRTIO_MSG_FFA_DEACTIVATE			0x02
#define VIRTIO_MSG_FFA_CONFIGURE			0x03
#define VIRTIO_MSG_FFA_AREA_SHARE			0x04
#define VIRTIO_MSG_FFA_AREA_UNSHARE			0x05

#define VIRTIO_MSG_MAX_SIZE				40

#define VIRTIO_MSG_TYPE_RESPONSE			(1 << 0)
#define VIRTIO_MSG_TYPE_VIRTIO				(0 << 1)
#define VIRTIO_MSG_TYPE_BUS				(1 << 1)

#define VIRTIO_MSG_FFA_VERSION_1_0			0x1

#define VIRTIO_MSG_FFA_FEATURE_INDIRECT_MSG_SUPP	(1 << 0)
#define VIRTIO_MSG_FFA_FEATURE_DIRECT_MSG_SUPP		(1 << 1)
#define VIRTIO_MSG_FFA_FEATURE_NUM_SHM			(0xFF << 8)

#define VIRTIO_MSG_FFA_AREA_ID_OFFSET			56
#define VIRTIO_MSG_FFA_OFFSET_MASK			((ULL(1) << VIRTIO_MSG_FFA_AREA_ID_OFFSET) - 1)

/* Message payload format */

struct bus_activate {
	__le32 driver_version;
} __attribute__((packed));

struct bus_activate_resp {
	__le32 device_version;
	__le64 features;
	__le64 num;
} __attribute__((packed));

struct bus_configure {
	__le64 features;
} __attribute__((packed));

struct bus_configure_resp {
	__le64 features;
} __attribute__((packed));

struct bus_area_share {
	__le32 area_id;
	__le64 mem_handle;
} __attribute__((packed));

struct bus_area_share_resp {
	__le32 area_id;
} __attribute__((packed));

struct bus_area_unshare {
	__le32 area_id;
	__le64 mem_handle;
} __attribute__((packed));

struct virtio_msg_ffa {
	__u8 type;
	__u8 id;
	__le16 unused;

	union {
		__u8 payload_u8[36];

		struct bus_activate bus_activate;
		struct bus_activate_resp bus_activate_resp;
		struct bus_configure bus_configure;
		struct bus_configure_resp bus_configure_resp;
		struct bus_area_share bus_area_share;
		struct bus_area_share_resp bus_area_share_resp;
		struct bus_area_unshare bus_area_unshare;
	};
} __attribute__((packed));

#endif /* _LINUX_VIRTIO_MSG_FFA_H */
