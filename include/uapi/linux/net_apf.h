/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * net_apf.h - Generic Netlink interface for Advanced Packet Filter (APF)
 *
 * This header defines the public interface for the net_apf generic
 * netlink family. This interface is intended to be a vendor-agnostic API for
 * configuring the Advanced Packet Filter (APF) on any network interface that
 * supports it, including wireless and Ethernet interfaces.
 *
 * Copyright 2026 Google LLC
 */

#ifndef _UAPI_LINUX_NET_APF_H
#define _UAPI_LINUX_NET_APF_H

#include <linux/types.h>

#define NET_APF_FAMILY_NAME	"net_apf"
#define NET_APF_FAMILY_VERSION	1
#define NET_APF_MAX_BUFFER_LEN	65535

/* Special offset representing full APF bytecode program installation */
#define NET_APF_PROGRAM_OFFSET	(-1)

/**
 * enum net_apf_commands - Generic Netlink commands for net_apf
 */
enum net_apf_commands {
	/**
	 * @NET_APF_CMD_UNSPEC: Unspecified command, reserved.
	 */
	NET_APF_CMD_UNSPEC,

	/**
	 * @NET_APF_CMD_GET_INFO: Query APF capabilities and device information.
	 *
	 * In:
	 *   - NET_APF_A_IFINDEX (u32, required): Netdev interface index.
	 * Out (on success):
	 *   - NET_APF_A_INFO_VERSION (u32): Interpreter version (e.g. 6100).
	 *   - NET_APF_A_INFO_ID (u32): Chip-level APF instance identifier.
	 *   - NET_APF_A_INFO_TOTAL_RAM (u32): Total RAM in bytes.
	 *   - NET_APF_A_INFO_USED_RAM (u32): Currently allocated RAM in bytes.
	 *   - NET_APF_A_INFO_OVERHEAD (u32): Per-interface state overhead.
	 *   - NET_APF_A_INFO_GRANULARITY (u32): Memory allocation granularity.
	 */
	NET_APF_CMD_GET_INFO,

	/**
	 * @NET_APF_CMD_SET_ID: Set chip-level APF instance identifier (placeholder).
	 *
	 * In:
	 *   - NET_APF_A_IFINDEX (u32, required): Netdev interface index.
	 *   - NET_APF_A_ID (u32, required): Chip-level APF instance identifier.
	 */
	NET_APF_CMD_SET_ID,

	/**
	 * @NET_APF_CMD_ENABLE: Allocate and enable per-interface APF state (placeholder).
	 *
	 * In:
	 *   - NET_APF_A_IFINDEX (u32, required): Netdev interface index.
	 *   - NET_APF_A_RAM_SIZE (u32, required): Requested RAM size in bytes.
	 */
	NET_APF_CMD_ENABLE,

	/**
	 * @NET_APF_CMD_DISABLE: Disable and free per-interface APF state (placeholder).
	 *
	 * In:
	 *   - NET_APF_A_IFINDEX (u32, required): Netdev interface index.
	 */
	NET_APF_CMD_DISABLE,

	/**
	 * @NET_APF_CMD_GET_RAM_SIZE: Query allocated RAM size for an interface (placeholder).
	 *
	 * In:
	 *   - NET_APF_A_IFINDEX (u32, required): Netdev interface index.
	 * Out (on success):
	 *   - NET_APF_A_RAM_SIZE (u32): Allocated RAM size in bytes.
	 */
	NET_APF_CMD_GET_RAM_SIZE,

	/**
	 * @NET_APF_CMD_READ: Read data from APF RAM at a specified byte offset.
	 *
	 * In:
	 *   - NET_APF_A_IFINDEX (u32, required): Netdev interface index.
	 *   - NET_APF_A_READ_OFFSET (u32, required): Non-negative byte offset.
	 *   - NET_APF_A_DATA_LEN (u32, required): Number of bytes to read.
	 * Out (on success):
	 *   - NET_APF_A_DATA (binary): Read buffer payload.
	 */
	NET_APF_CMD_READ,

	/**
	 * @NET_APF_CMD_WRITE: Write data to APF RAM or install a bytecode program.
	 *
	 * In:
	 *   - NET_APF_A_IFINDEX (u32, required): Netdev interface index.
	 *   - NET_APF_A_WRITE_OFFSET (s32, required): Byte offset into APF RAM,
	 *       or NET_APF_PROGRAM_OFFSET (-1) to install a new bytecode program.
	 *   - NET_APF_A_DATA (binary, required): Bytecode program or data buffer.
	 *   - NET_APF_A_DATA_LEN (u32, optional): Payload length for validation.
	 */
	NET_APF_CMD_WRITE,

	/**
	 * @NET_APF_CMD_SET_FAST_PATH: Configure hardware/driver APF fast path filtering.
	 *
	 * In:
	 *   - NET_APF_A_IFINDEX (u32, required): Netdev interface index.
	 *   - NET_APF_A_FP_UCAST_MAC (binary, 6 bytes, required): Unicast MAC address.
	 *   - NET_APF_A_FP_VLAN_TAG (s16, required): VLAN tag ID (-1 to disable).
	 *   - NET_APF_A_FP_UCAST_ADDR4 (be32, required): Unicast IPv4 address (0.0.0.0 to disable).
	 *   - NET_APF_A_FP_ENABLE_IPV6 (u8, required): IPv6 fastpath enable flag (1/0).
	 */
	NET_APF_CMD_SET_FAST_PATH,

	/* private: */
	__NET_APF_CMD_MAX,
	NET_APF_CMD_MAX = (__NET_APF_CMD_MAX - 1)
};

/**
 * enum net_apf_attributes - Generic Netlink attributes for net_apf
 */
enum net_apf_attributes {
	/**
	 * @NET_APF_A_UNSPEC: Unspecified attribute, reserved.
	 */
	NET_APF_A_UNSPEC,

	/**
	 * @NET_APF_A_IFINDEX: (u32) Netdev interface index.
	 *   Applies to: All commands (In).
	 */
	NET_APF_A_IFINDEX,

	/*
	 * Attributes returned by NET_APF_CMD_GET_INFO (Out)
	 */

	/**
	 * @NET_APF_A_INFO_VERSION: (u32) Supported APF interpreter version (e.g. 6100).
	 *   Applies to: NET_APF_CMD_GET_INFO (Out).
	 */
	NET_APF_A_INFO_VERSION,

	/**
	 * @NET_APF_A_INFO_ID: (u32) Chip-level APF instance identifier.
	 *   Applies to: NET_APF_CMD_GET_INFO (Out).
	 */
	NET_APF_A_INFO_ID,

	/**
	 * @NET_APF_A_INFO_TOTAL_RAM: (u32) Total APF RAM size in bytes.
	 *   Applies to: NET_APF_CMD_GET_INFO (Out).
	 */
	NET_APF_A_INFO_TOTAL_RAM,

	/**
	 * @NET_APF_A_INFO_USED_RAM: (u32) Currently allocated APF RAM in bytes.
	 *   Applies to: NET_APF_CMD_GET_INFO (Out).
	 */
	NET_APF_A_INFO_USED_RAM,

	/**
	 * @NET_APF_A_INFO_OVERHEAD: (u32) Per-interface state overhead in bytes.
	 *   Applies to: NET_APF_CMD_GET_INFO (Out).
	 */
	NET_APF_A_INFO_OVERHEAD,

	/**
	 * @NET_APF_A_INFO_GRANULARITY: (u32) Memory allocation granularity in bytes.
	 *   Applies to: NET_APF_CMD_GET_INFO (Out).
	 */
	NET_APF_A_INFO_GRANULARITY,

	/*
	 * Configuration and data transfer attributes
	 */

	/**
	 * @NET_APF_A_ID: (u32) Chip-level APF instance identifier.
	 *   Applies to: NET_APF_CMD_SET_ID (In).
	 */
	NET_APF_A_ID,

	/**
	 * @NET_APF_A_RAM_SIZE: (u32) RAM size in bytes.
	 *   Applies to: NET_APF_CMD_ENABLE (In), NET_APF_CMD_GET_RAM_SIZE (Out).
	 */
	NET_APF_A_RAM_SIZE,

	/**
	 * @NET_APF_A_READ_OFFSET: (u32) Non-negative byte offset into APF RAM.
	 *   Applies to: NET_APF_CMD_READ (In).
	 */
	NET_APF_A_READ_OFFSET,

	/**
	 * @NET_APF_A_WRITE_OFFSET: (s32) Byte offset into APF RAM (-1 for program installation).
	 *   Applies to: NET_APF_CMD_WRITE (In).
	 */
	NET_APF_A_WRITE_OFFSET,

	/**
	 * @NET_APF_A_DATA: (binary) Bytecode program or memory data buffer.
	 *   Applies to: NET_APF_CMD_READ (Out), NET_APF_CMD_WRITE (In).
	 */
	NET_APF_A_DATA,

	/**
	 * @NET_APF_A_DATA_LEN: (u32) Length of data in bytes.
	 *   Applies to: NET_APF_CMD_READ (In), NET_APF_CMD_WRITE (In, optional).
	 */
	NET_APF_A_DATA_LEN,

	/*
	 * Fast path filtering attributes
	 */

	/**
	 * @NET_APF_A_FP_UCAST_MAC: (binary, 6 bytes) Interface unicast MAC address.
	 *   Applies to: NET_APF_CMD_SET_FAST_PATH (In).
	 */
	NET_APF_A_FP_UCAST_MAC,

	/**
	 * @NET_APF_A_FP_VLAN_TAG: (s16) VLAN tag ID to filter (-1 to disable).
	 *   Applies to: NET_APF_CMD_SET_FAST_PATH (In).
	 */
	NET_APF_A_FP_VLAN_TAG,

	/**
	 * @NET_APF_A_FP_UCAST_ADDR4: (be32) Interface unicast IPv4 address (0.0.0.0 to disable).
	 *   Applies to: NET_APF_CMD_SET_FAST_PATH (In).
	 */
	NET_APF_A_FP_UCAST_ADDR4,

	/**
	 * @NET_APF_A_FP_ENABLE_IPV6: (u8) Boolean flag enabling IPv6 fast path (1/0).
	 *   Applies to: NET_APF_CMD_SET_FAST_PATH (In).
	 */
	NET_APF_A_FP_ENABLE_IPV6,

	/* private: */
	__NET_APF_A_MAX,
	NET_APF_A_MAX = (__NET_APF_A_MAX - 1)
};

#endif /* _UAPI_LINUX_NET_APF_H */
