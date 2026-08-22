/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * apf.h - Advanced Packet Filter (APF) kernel interface
 *
 * Copyright 2024-2026 Google LLC
 */

#ifndef _NET_APF_H
#define _NET_APF_H

#include <linux/types.h>
#include <linux/if_ether.h>

struct net_device;

/**
 * struct apf_ops - Advanced Packet Filter device operations
 * @get_caps: Query APF capabilities for the network device.
 * @set_filter: Install an APF bytecode program onto the device.
 * @get_filter: Read currently installed APF program from the device.
 * @fast_path: Configure hardware/driver APF fast path filtering parameters.
 */
struct apf_ops {
	int (*get_caps)(struct net_device *dev, u32 *version,
			u32 *max_program_size, u32 *flags);
	int (*set_filter)(struct net_device *dev, const u8 *program,
			  u32 len, const u32 flags);
	int (*get_filter)(struct net_device *dev, u8 *program, u32 *len);
	int (*fast_path)(struct net_device *dev, u8 ucast_mac[ETH_ALEN],
			 s16 vlan_tag, __be32 ucast_addr4,
			 bool enable_ipv6_fastpath);
};

#endif /* _NET_APF_H */
