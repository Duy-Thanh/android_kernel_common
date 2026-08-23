/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Advanced Packet Filter (APF) kernel interface
 *
 * Copyright 2026 Google LLC
 */

#ifndef _NET_APF_H
#define _NET_APF_H

#include <linux/types.h>
#include <linux/if_ether.h>
#include <uapi/linux/net_apf.h>

struct net_device;
struct netlink_ext_ack;

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

/**
 * struct apf_ops - Advanced Packet Filter driver callbacks
 *
 * Driver callback implementations must return 0 on success (or non-negative
 * value where applicable) or a standard negative errno code on failure (-Exxx).
 *
 * In case of error (i.e. negative return value), additional useful extended
 * Netlink error messages can be reported back to userspace by setting extack
 * (e.g. NL_SET_ERR_MSG(extack, "Firmware went out for lunch") or
 * NL_SET_ERR_MSG_MOD(extack, "...")).
 *
 * Note: Userspace Netlink libraries (e.g. libnl nl_syserr2nlerr()) only
 * translate the following set of error codes (everything else becomes
 * NLE_FAILURE):
 *   - EBADF           -> NLE_BAD_SOCK
 *   - EADDRINUSE      -> NLE_EXIST
 *   - EEXIST          -> NLE_EXIST
 *   - EADDRNOTAVAIL   -> NLE_NOADDR
 *   - ESRCH           -> NLE_OBJ_NOTFOUND
 *   - ENOENT          -> NLE_OBJ_NOTFOUND
 *   - EINTR           -> NLE_INTR
 *   - EAGAIN          -> NLE_AGAIN
 *   - ENOTSOCK        -> NLE_BAD_SOCK
 *   - ENOPROTOOPT     -> NLE_INVAL
 *   - EFAULT          -> NLE_INVAL
 *   - EACCES          -> NLE_NOACCESS
 *   - EINVAL          -> NLE_INVAL
 *   - ENOBUFS         -> NLE_NOMEM
 *   - ENOMEM          -> NLE_NOMEM
 *   - EAFNOSUPPORT    -> NLE_AF_NOSUPPORT
 *   - EPROTONOSUPPORT -> NLE_PROTO_MISMATCH
 *   - EOPNOTSUPP      -> NLE_OPNOTSUPP
 *   - EPERM           -> NLE_PERM
 *   - EBUSY           -> NLE_BUSY
 *   - ERANGE          -> NLE_RANGE
 *   - ENODEV          -> NLE_NODEV
 *   - EHOSTUNREACH    -> NLE_HOSTUNREACH
 */
struct apf_ops {
	// Version of struct apf_ops (current version is 0).
	u32 version;

	// Query APF information and capabilities for the network device.
	int (*get_info)(struct net_device *dev, struct netlink_ext_ack *extack,
			struct apf_info *info);

	// Set chip-level APF instance identifier.
	int (*set_id)(struct net_device *dev, struct netlink_ext_ack *extack,
		      u32 id);

	// Allocate and enable per-interface APF state.
	int (*enable)(struct net_device *dev, struct netlink_ext_ack *extack,
		      u32 ram_size);

	/**
	 * Query allocated RAM size for this interface.
	 *
	 * Note: In APFv8, allocated RAM size only changes upon enable/disable;
	 * in APFv6.1, there is no enable/disable and it always equals total_ram.
	 * In both versions, the value should be cached.
	 */
	int (*get_ram_size)(struct net_device *dev,
			    struct netlink_ext_ack *extack);

	/**
	 * Read buffer at a non-negative byte offset from APF RAM (APFv6.1 only
	 * supports offset == 0 and len == total_ram).
	 */
	int (*read)(struct net_device *dev, struct netlink_ext_ack *extack,
		    u32 offset, u8 *buf, u32 len);

	/**
	 * Write buffer at a byte offset into APF RAM (offset -1 installs program;
	 * APFv6.1 only supports offset == -1).
	 */
	int (*write)(struct net_device *dev, struct netlink_ext_ack *extack,
		     s32 offset, const u8 *buf, u32 len);

	// Disable and free per-interface APF state.
	int (*disable)(struct net_device *dev, struct netlink_ext_ack *extack);

	// Configure hardware/driver APF fast path filtering parameters.
	int (*fast_path)(struct net_device *dev, struct netlink_ext_ack *extack,
			 const u8 ucast_mac[ETH_ALEN], s16 vlan_tag,
			 __be32 ucast_addr4, bool enable_ipv6_fastpath);
};

#endif // _NET_APF_H
