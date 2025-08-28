/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SELINUX_POLICYCAP_NAMES_H_
#define _SELINUX_POLICYCAP_NAMES_H_

#include "policycap.h"

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
	// ANDROID: Maintain array positions by adding padding for
	// policycaps which have not been backported
	"userspace_initial_context_NOT_IMPLEMENTED",
	"netlink_xperm_NOT_IMPLEMENTED",
	"netif_wildcard_NOT_IMPLEMENTED",
	"genfs_seclabel_wildcard_NOT_IMPLEMENTED",
	"functionfs_seclabel"
};

#endif /* _SELINUX_POLICYCAP_NAMES_H_ */
