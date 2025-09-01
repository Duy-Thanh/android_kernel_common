/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018 Western Digital Corporation
 */

#ifndef __UFS_SYSFS_H__
#define __UFS_SYSFS_H__

#include <linux/sysfs.h>

struct device;

void ufs_sysfs_add_nodes(struct device *dev);
void ufs_sysfs_remove_nodes(struct device *dev);

extern const struct attribute_group ufs_sysfs_unit_descriptor_group;
extern const struct attribute_group ufs_sysfs_lun_attributes_group;
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
extern const struct attribute_group ufs_sysfs_hid_group;
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

#endif
