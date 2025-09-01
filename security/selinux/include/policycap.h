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
	__POLICYDB_CAP_MAX
};
#define POLICYDB_CAP_MAX (__POLICYDB_CAP_MAX - 1)

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
/*
 * ANDROID: Define this outside of the enum to preserve the KMI.
 *
 * This value must match what userspace expects the capability number to be.
 */
#define POLICYDB_CAP_MEMFD_CLASS 13

||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
extern const char *const selinux_policycap_names[__POLICYDB_CAP_MAX];

#endif /* _SELINUX_POLICYCAP_H_ */
