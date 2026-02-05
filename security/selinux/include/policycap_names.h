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
<<<<<<< HEAD   (7fa33ba90b34f99e816cb1719d346f70b5be0627 UPSTREAM: selinux: support wildcard match in genfscon am: 48)
	"netlink_xperm",
	"netif_wildcard",
	"genfs_seclabel_wildcard",
||||||| BASE   (4806465da60368a169da905e89f148f5783d7400 UPSTREAM: selinux: support wildcard match in genfscon)
	"genfs_seclabel_wildcard",
=======
>>>>>>> BRANCH (476c615f551c957317be7c3e3c1dce31efd377be ANDROID: selinux: fix ABI break by genfscon wildcard)
};
/* clang-format on */

#endif /* _SELINUX_POLICYCAP_NAMES_H_ */
