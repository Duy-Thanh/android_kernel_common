/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Google LLC
 */

#ifndef _INCFS_VERITY_H
#define _INCFS_VERITY_H

#ifdef CONFIG_FS_VERITY

int ioctl_enable_verity(struct file *filp, const void __user *uarg);

int incfs_verity_get_flags(struct file *f, void __user *arg);

int incfs_fsverity_file_open(struct inode *inode, struct file *filp);

int incfs_verity_measure(struct file *filp, void __user *_uarg);

#else /* !CONFIG_FS_VERITY */

static int ioctl_enable_verity(struct file *filp, const void __user *uarg)
{
	return -EOPNOTSUPP;
}

static inline int incfs_verity_get_flags(struct file *f, void __user *arg)
{
	return -EOPNOTSUPP;
}

static inline int incfs_fsverity_file_open(struct inode *inode,
					   struct file *filp)
{
	return -EOPNOTSUPP;
}

static inline int incfs_verity_measure(struct file *filp, void __user *_uarg)
{
	return -EOPNOTSUPP;
}

#endif /* !CONFIG_FS_VERITY */

#endif
