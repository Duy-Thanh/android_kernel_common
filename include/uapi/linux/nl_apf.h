/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * nl_apf.h - Generic Netlink interface for Advanced Packet Filter (APF)
 *
 * This header defines the public interface for the nl_apf generic
 * netlink family. This interface is intended to be a vendor-agnostic API for
 * configuring the Advanced Packet Filter (APF) on any network interface that
 * supports it, including wireless and Ethernet interfaces.
 *
 * Copyright 2024-2026 Google LLC
 */

#ifndef _UAPI_LINUX_NL_APF_H
#define _UAPI_LINUX_NL_APF_H

#include <linux/types.h>

#define NL_APF_FAMILY_NAME	"nl_apf"
#define NL_APF_VERSION		1
#define NL_APF_MAX_PROGRAM_LEN	4096

/**
 * enum nl_apf_commands - Supported commands
 *
 * @NL_APF_CMD_UNSPEC: Unspecified command.
 * @NL_APF_CMD_GET_CAPABILITIES: Query the device for its APF
 *	capabilities. Requires NL_APF_ATTR_IFINDEX in the request.
 *	Returns attributes for version, max program size, and flags.
 * @NL_APF_CMD_SET_FILTER: Set/install an APF program on the device.
 *	Requires NL_APF_ATTR_IFINDEX and NL_APF_ATTR_PROGRAM.
 * @NL_APF_CMD_GET_FILTER: Retrieve the currently installed APF program
 *	from the device. Requires NL_APF_ATTR_IFINDEX. Returns the
 *	program in NL_APF_ATTR_PROGRAM.
 * @NL_APF_CMD_SET_FAST_PATH: Configure hardware/driver APF fast path
 *	filtering parameters (unicast MAC, VLAN tag, unicast IPv4 address,
 *	and IPv6 fast path enablement).
 */
enum nl_apf_commands {
	NL_APF_CMD_UNSPEC,
	NL_APF_CMD_GET_CAPABILITIES,
	NL_APF_CMD_SET_FILTER,
	NL_APF_CMD_GET_FILTER,
	NL_APF_CMD_SET_FAST_PATH,
	__NL_APF_CMD_MAX,
};
#define NL_APF_CMD_MAX (__NL_APF_CMD_MAX - 1)

/**
 * enum nl_apf_attributes - Netlink attributes
 *
 * @NL_APF_ATTR_UNSPEC: Unspecified attribute.
 * @NL_APF_ATTR_IFINDEX: (u32) The netdev interface index to operate on.
 * @NL_APF_ATTR_VERSION: (u32) The APF interpreter version supported.
 * @NL_APF_ATTR_MAX_PROGRAM_SIZE: (u32) The maximum size of an APF
 *	program in bytes that the hardware can store.
 * @NL_APF_ATTR_PROGRAM: (binary) The APF bytecode program.
 * @NL_APF_ATTR_PROGRAM_LEN: (u32) The length of the program in bytes.
 *	Used in the GET_FILTER response.
 * @NL_APF_ATTR_FLAGS: (u32) A bitmask of flags. See NL_APF_FLAG_*.
 * @NL_APF_ATTR_UCAST_MAC: (binary, 6 bytes) The interface's unicast MAC address.
 * @NL_APF_ATTR_VLAN_TAG: (s16) VLAN tag filtering (-1 to disable).
 * @NL_APF_ATTR_UCAST_ADDR4: (be32) The interface's unicast IPv4 address (0.0.0.0 to disable).
 * @NL_APF_ATTR_ENABLE_IPV6_FASTPATH: (u8) Whether IPv6 fast path is
 *	enabled (1 = enabled, 0 = disabled).
 */
enum nl_apf_attributes {
	NL_APF_ATTR_UNSPEC,
	NL_APF_ATTR_IFINDEX,
	NL_APF_ATTR_VERSION,
	NL_APF_ATTR_MAX_PROGRAM_SIZE,
	NL_APF_ATTR_PROGRAM,
	NL_APF_ATTR_PROGRAM_LEN,
	NL_APF_ATTR_FLAGS,
	NL_APF_ATTR_UCAST_MAC,
	NL_APF_ATTR_VLAN_TAG,
	NL_APF_ATTR_UCAST_ADDR4,
	NL_APF_ATTR_ENABLE_IPV6_FASTPATH,
	__NL_APF_ATTR_MAX,
};
#define NL_APF_ATTR_MAX (__NL_APF_ATTR_MAX - 1)

/**
 * enum nl_apf_flags - Feature and status flags
 *
 * @NL_APF_FLAG_FILTER_ENABLED: Indicates if the APF filter is
 *	currently active and processing packets. Returned by GET_CAPABILITIES.
 * @NL_APF_FLAG_ALLOW_DUPLICATE_SET: A flag for SET_FILTER to indicate
 *	that setting the same program again should be allowed. The default
 *	behavior should be to reject a program that is already installed.
 */
enum nl_apf_flags {
	NL_APF_FLAG_FILTER_ENABLED	= 1 << 0,
	NL_APF_FLAG_ALLOW_DUPLICATE_SET	= 1 << 1,
};

#endif /* _UAPI_LINUX_NL_APF_H */
