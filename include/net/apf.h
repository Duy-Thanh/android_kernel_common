/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * apf.h - Advanced Packet Filter (APF) kernel interface
 *
 * Copyright 2026 Google LLC
 */

#ifndef _NET_APF_H
#define _NET_APF_H

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/android_kabi.h>
#include <uapi/linux/net_apf.h>

struct net_device;

/**
 * struct apf_info - APF device information and capabilities
 */
struct apf_info {
	/**
	 * @apf_version: APF interpreter version (e.g. 6100 represents APFv6.1).
	 */
	u32 apf_version;

	/**
	 * @apf_id: Chip-level APF instance identifier (defaults to 0).
	 */
	u32 apf_id;

	/**
	 * @total_ram: Total APF RAM in bytes.
	 */
	u32 total_ram;

	/**
	 * @used_ram: Currently allocated APF RAM in bytes.
	 */
	u32 used_ram;

	/**
	 * @overhead: Per-interface state overhead in bytes.
	 */
	u32 overhead;

	/**
	 * @granularity: Memory allocation granularity in bytes.
	 */
	u32 granularity;
};

/**
 * struct apf_ops - Advanced Packet Filter device operations
 */
struct apf_ops {
	/**
	 * @get_info: Query APF information and capabilities for the network device.
	 */
	int (*get_info)(struct net_device *dev, struct apf_info *info);

	/**
	 * @read: Read buffer at a non-negative byte offset from APF RAM.
	 */
	int (*read)(struct net_device *dev, u32 offset, u8 *buf, u32 len);

	/**
	 * @write: Write buffer at a byte offset into APF RAM (offset -1 installs program).
	 */
	int (*write)(struct net_device *dev, s32 offset, const u8 *buf, u32 len);

	/**
	 * @fast_path: Configure hardware/driver APF fast path filtering parameters.
	 */
	int (*fast_path)(struct net_device *dev, const u8 ucast_mac[ETH_ALEN],
			 s16 vlan_tag, __be32 ucast_addr4,
			 bool enable_ipv6_fastpath);

	/**
	 * @set_id: Set chip-level APF instance identifier (placeholder / optional).
	 */
	int (*set_id)(struct net_device *dev, u32 id);

	/**
	 * @enable: Allocate and enable per-interface APF state (placeholder / optional).
	 */
	int (*enable)(struct net_device *dev, u32 ram_size);

	/**
	 * @disable: Disable and free per-interface APF state (placeholder / optional).
	 */
	int (*disable)(struct net_device *dev);

	/**
	 * @get_ram_size: Query allocated RAM size for this interface (placeholder / optional).
	 */
	u32 (*get_ram_size)(struct net_device *dev);

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
	ANDROID_KABI_RESERVE(5);
	ANDROID_KABI_RESERVE(6);
	ANDROID_KABI_RESERVE(7);
	ANDROID_KABI_RESERVE(8);
};

#endif /* _NET_APF_H */
