/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * Generic Netlink interface for Advanced Packet Filter (APF)
 *
 * This header defines the public interface for the 'net_apf' generic
 * netlink family. This interface is intended to be a vendor-agnostic API for
 * configuring the Advanced Packet Filter (APF) on any network interface that
 * supports it, including wireless and ethernet interfaces.
 *
 * Copyright 2026 Google LLC
 */

#ifndef _UAPI_LINUX_NET_APF_H
#define _UAPI_LINUX_NET_APF_H

#include <linux/types.h>

#define APF_FAMILY_NAME		"net_apf"
#define APF_FAMILY_VERSION	1

/**
 * enum apf_commands - Generic Netlink commands for 'net_apf'
 */
enum apf_commands {
	// Unspecified command, reserved.
	APF_CMD_UNSPEC,

	/**
	 * @APF_CMD_GET_INFO: Query APF capabilities and device information.
	 *
	 * In:
	 *   - APF_ATTR_IFINDEX (u32): Netdev interface index.
	 * Out (on success):
	 *   - APF_ATTR_INFO_VERSION (u32): APF interpreter version (e.g. 6100).
	 *   - APF_ATTR_INFO_ID (u32): Chip-level APF instance identifier.
	 *   - APF_ATTR_INFO_TOTAL_RAM (u32): Total APF RAM size in bytes.
	 *   - APF_ATTR_INFO_USED_RAM (u32): Currently allocated APF RAM in bytes.
	 *   - APF_ATTR_INFO_OVERHEAD (u32): Per-interface state overhead in bytes.
	 *   - APF_ATTR_INFO_GRANULARITY (u32): Memory allocation granularity in bytes.
	 */
	APF_CMD_GET_INFO,

	/**
	 * @APF_CMD_SET_ID: Set chip-level APF instance identifier.
	 *
	 * In:
	 *   - APF_ATTR_IFINDEX (u32): Netdev interface index.
	 *   - APF_ATTR_INFO_ID (u32): Chip-level APF instance identifier.
	 */
	APF_CMD_SET_ID,

	/**
	 * @APF_CMD_ENABLE: Allocate and enable per-interface APF state.
	 *
	 * In:
	 *   - APF_ATTR_IFINDEX (u32): Netdev interface index.
	 *   - APF_ATTR_RAM_SIZE (u32): Requested RAM size in bytes.
	 */
	APF_CMD_ENABLE,

	/**
	 * @APF_CMD_GET_RAM_SIZE: Query allocated RAM size for an interface.
	 *
	 * In:
	 *   - APF_ATTR_IFINDEX (u32): Netdev interface index.
	 * Out (on success):
	 *   - APF_ATTR_RAM_SIZE (u32): Allocated RAM size in bytes.
	 */
	APF_CMD_GET_RAM_SIZE,

	/**
	 * @APF_CMD_READ: Read data from APF RAM at a specified byte offset
	 *   (APFv6.1 only supports offset == 0 and len == -1).
	 *
	 * In:
	 *   - APF_ATTR_IFINDEX (u32): Netdev interface index.
	 *   - APF_ATTR_OFFSET (s32): Non-negative byte offset.
	 *   - APF_ATTR_DATA_LEN (s32): Number of bytes to read, or -1 for all.
	 * Out (on success):
	 *   - APF_ATTR_DATA (binary): Read buffer payload.
	 */
	APF_CMD_READ,

	/**
	 * @APF_CMD_WRITE: Write data to APF RAM or install a bytecode program
	 *   (APFv6.1 only supports offset == -1).
	 *
	 * In:
	 *   - APF_ATTR_IFINDEX (u32): Netdev interface index.
	 *   - APF_ATTR_OFFSET (s32): Byte offset into APF RAM,
	 *       or -1 to install a new bytecode program.
	 *   - APF_ATTR_DATA (binary): Bytecode program or data buffer.
	 */
	APF_CMD_WRITE,

	/**
	 * @APF_CMD_DISABLE: Disable and free per-interface APF state.
	 *
	 * In:
	 *   - APF_ATTR_IFINDEX (u32): Netdev interface index.
	 */
	APF_CMD_DISABLE,

	/**
	 * @APF_CMD_SET_FAST_PATH: Configure hardware/driver APF fast path filtering.
	 *
	 * In:
	 *   - APF_ATTR_IFINDEX (u32): Netdev interface index.
	 *   - APF_ATTR_FP_UCAST_MAC (binary, 6 bytes): Interface unicast MAC address.
	 *   - APF_ATTR_FP_VLAN_TAG (s16): VLAN tag ID to filter (-1 to disable).
	 *   - APF_ATTR_FP_UCAST_ADDR4 (be32): Interface unicast IPv4 address (0.0.0.0 to disable).
	 *   - APF_ATTR_FP_ENABLE_IPV6 (u8): Boolean flag enabling IPv6 fast path (1/0).
	 */
	APF_CMD_SET_FAST_PATH,

	// private:
	__APF_CMD_MAX,
};

#define APF_CMD_MAX	(__APF_CMD_MAX - 1)

/**
 * enum apf_attributes - Generic Netlink attributes for 'net_apf'
 */
enum apf_attributes {
	// Unspecified attribute, reserved.
	APF_ATTR_UNSPEC,
	APF_ATTR_IFINDEX,
	APF_ATTR_INFO_VERSION,
	APF_ATTR_INFO_ID,
	APF_ATTR_INFO_TOTAL_RAM,
	APF_ATTR_INFO_USED_RAM,
	APF_ATTR_INFO_OVERHEAD,
	APF_ATTR_INFO_GRANULARITY,
	APF_ATTR_RAM_SIZE,
	APF_ATTR_OFFSET,
	APF_ATTR_DATA,
	APF_ATTR_DATA_LEN,
	APF_ATTR_FP_UCAST_MAC,
	APF_ATTR_FP_VLAN_TAG,
	APF_ATTR_FP_UCAST_ADDR4,
	APF_ATTR_FP_ENABLE_IPV6,
	// private:
	__APF_ATTR_MAX,
};

#define APF_ATTR_MAX	(__APF_ATTR_MAX - 1)

#endif // _UAPI_LINUX_NET_APF_H
