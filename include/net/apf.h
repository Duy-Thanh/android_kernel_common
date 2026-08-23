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
#include <uapi/linux/net_apf.h>

struct net_device;

struct apf_info {
	// APF interpreter version (e.g. 6100 represents APFv6.1).
	u32 apf_version;
	// Chip-level APF instance identifier (defaults to 0).
	u32 apf_id;
	// Total APF RAM in bytes.
	u32 total_ram;
	// Currently allocated APF RAM in bytes.
	u32 used_ram;
	// Per-interface state overhead in bytes.
	u32 overhead;
	// Memory allocation granularity in bytes.
	u32 granularity;
};

struct apf_ops {
	// Version of struct apf_ops.
	u32 version;

	// Query APF information and capabilities for the network device.
	int (*get_info)(struct net_device *dev, struct apf_info *info);

	// Read buffer at a non-negative byte offset from APF RAM.
	int (*read)(struct net_device *dev, u32 offset, u8 *buf, u32 len);

	// Write buffer at a byte offset into APF RAM (offset -1 installs program).
	int (*write)(struct net_device *dev, s32 offset, const u8 *buf, u32 len);

	// Configure hardware/driver APF fast path filtering parameters.
	int (*fast_path)(struct net_device *dev, const u8 ucast_mac[ETH_ALEN],
			 s16 vlan_tag, __be32 ucast_addr4,
			 bool enable_ipv6_fastpath);

	// Set chip-level APF instance identifier.
	int (*set_id)(struct net_device *dev, u32 id);

	// Allocate and enable per-interface APF state.
	int (*enable)(struct net_device *dev, u32 ram_size);

	// Disable and free per-interface APF state.
	int (*disable)(struct net_device *dev);

	// Query allocated RAM size for this interface.
	u32 (*get_ram_size)(struct net_device *dev);
};

#endif // _NET_APF_H
