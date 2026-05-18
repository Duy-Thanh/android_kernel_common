/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SELINUX_POLICYCAP_NAMES_H_
#define _SELINUX_POLICYCAP_NAMES_H_

#include "policycap.h"

/* clang-format off */
/* Policy capability names */
const char *const selinux_policycap_names[__POLICYDB_CAP_MAX] = {
	"network_peer_controls",
	"open_perms",
	"extended_socket_class",
	"always_check_network",
	"cgroup_seclabel",
	"nnp_nosuid_transition",
	"genfs_seclabel_symlinks",
	"ioctl_skip_cloexec",
	"userspace_initial_context",
};

/* clang-format on */

/* ANDROID: Names defined outside to preserve the KMI. */
const char *const selinux_policycap_names_android[__POLICYDB_CAP_MAX_ANDROID -
						  __POLICYDB_CAP_MAX] = {
	"netlink_xperm",
	"netif_wildcard",
	"genfs_seclabel_wildcard",
	"memfd_class",
};

/* ANDROID: Holds if a capability is cherry-picked or not.  Use _Bool instead of
 * bool to keep this compatible with user-space files for ima.c
 */
const _Bool selinux_policycap_implemented_android[__POLICYDB_CAP_MAX_ANDROID -
						  __POLICYDB_CAP_MAX] = {
	0, /* POLICYDB_CAP_NETLINK_XPERM */
	0, /* POLICYDB_CAP_NETIF_WILDCARD */
	1, /* POLICYDB_CAP_GENFS_SECLABEL_WILDCARD */
	1, /* POLICYDB_CAP_MEMFD_CLASS */
};

#endif /* _SELINUX_POLICYCAP_NAMES_H_ */
