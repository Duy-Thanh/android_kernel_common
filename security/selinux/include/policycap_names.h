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
<<<<<<< HEAD   (d9bb8c2c71e58fc370c7c5437a0e058361602981 ANDROID: sched: Fix kernelci build-break: undefined symbol a)
||||||| BASE   (36837d71a847eda589e01964f2ef6de2263373a7 FROMGIT: ASoC: mediatek: Use SND_JACK_AVOUT for HDMI/DP jack)
=======
	"netlink_xperm",
	"netif_wildcard",
	"genfs_seclabel_wildcard",
>>>>>>> CHANGE (75c950e4a7a530970f6b70624e251c1ab939bcea BACKPORT: selinux: support wildcard match in genfscon)
};
/* clang-format on */

#endif /* _SELINUX_POLICYCAP_NAMES_H_ */
