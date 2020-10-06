/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC
 */

#ifndef _INCFS_VERITY_H
#define _INCFS_VERITY_H

#ifdef CONFIG_FS_VERITY

int incfs_ioctl_enable_verity(struct file *filp, const void __user *uarg);
int incfs_ioctl_verity_get_flags(struct file *f, void __user *arg);

#else /* !CONFIG_FS_VERITY */

static inline int incfs_ioctl_enable_verity(struct file *filp,
					    const void __user *uarg)
{
	return -EOPNOTSUPP;
}

static inline int incfs_ioctl_verity_get_flags(struct file *f, void __user *arg)
{
	return -EOPNOTSUPP;
}

#endif /* !CONFIG_FS_VERITY */

#endif
