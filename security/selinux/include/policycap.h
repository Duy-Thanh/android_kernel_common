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
<<<<<<< HEAD   (7df1d142e511f9f528c006ecf3c33c7d120fe6df FROMLIST: thermal: intel: int340x: Enable power slider inter)
	POLICYDB_CAP_NETLINK_XPERM,
	POLICYDB_CAP_NETIF_WILDCARD,
||||||| BASE   (189046767b10c7de900a253437d13f761f371dca Revert "ANDROID: mm: create vendor hooks for mm  proactive c)
=======
>>>>>>> BRANCH (4806465da60368a169da905e89f148f5783d7400 UPSTREAM: selinux: support wildcard match in genfscon)
	POLICYDB_CAP_GENFS_SECLABEL_WILDCARD,
	__POLICYDB_CAP_MAX
};
#define POLICYDB_CAP_MAX (__POLICYDB_CAP_MAX - 1)

/*
 * ANDROID: Define this outside of the enum to preserve the KMI.
 *
 * This value must match what userspace expects the capability number to be.
 */
#define POLICYDB_CAP_MEMFD_CLASS 13
#define POLICYDB_CAP_MEMFD_CLASS_NAME "memfd_class"

extern const char *const selinux_policycap_names[__POLICYDB_CAP_MAX];

#endif /* _SELINUX_POLICYCAP_H_ */
