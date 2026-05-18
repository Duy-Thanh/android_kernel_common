/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SELINUX_POLICYCAP_H_
#define _SELINUX_POLICYCAP_H_

/* Policy capabilities */
enum {
	POLICYDB_CAP_NETPEER,
	POLICYDB_CAP_OPENPERM,
	POLICYDB_CAP_EXTSOCKCLASS,
	POLICYDB_CAP_ALWAYSNETWORK,
	POLICYDB_CAP_CGROUPSECLABEL,
	POLICYDB_CAP_NNP_NOSUID_TRANSITION,
	POLICYDB_CAP_GENFS_SECLABEL_SYMLINKS,
	POLICYDB_CAP_IOCTL_SKIP_CLOEXEC,
	POLICYDB_CAP_USERSPACE_INITIAL_CONTEXT,
	__POLICYDB_CAP_MAX,
	/*
	 * ANDROID: Cherry-pick this to outside of the enum to preserve the KMI.
	 *
	 * This value must match what userspace expects the capability number to be.
	 */
	POLICYDB_CAP_NETLINK_XPERM =
		__POLICYDB_CAP_MAX, /* ANDROID: unimplemented */
	POLICYDB_CAP_NETIF_WILDCARD, /* ANDROID: unimplemented */
	POLICYDB_CAP_GENFS_SECLABEL_WILDCARD,
	POLICYDB_CAP_MEMFD_CLASS,
	__POLICYDB_CAP_MAX_ANDROID
};
#define POLICYDB_CAP_MAX (__POLICYDB_CAP_MAX - 1)
#define POLICYDB_CAP_MAX_ANDROID (__POLICYDB_CAP_MAX_ANDROID - 1)

extern const char *const selinux_policycap_names[__POLICYDB_CAP_MAX];

extern const char *const
	selinux_policycap_names_android[__POLICYDB_CAP_MAX_ANDROID -
					__POLICYDB_CAP_MAX];
extern const _Bool
	selinux_policycap_implemented_android[__POLICYDB_CAP_MAX_ANDROID -
					      __POLICYDB_CAP_MAX];

#endif /* _SELINUX_POLICYCAP_H_ */
