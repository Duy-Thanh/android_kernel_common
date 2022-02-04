/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018 Google LLC
 */

#ifndef _INCFS_VFS_H
#define _INCFS_VFS_H

void incfs_kill_sb(struct super_block *sb);
struct dentry *incfs_mount_fs(struct file_system_type *type, int flags,
			      const char *dev_name, void *data);
<<<<<<< HEAD   (46778c FROMGIT: f2fs: avoid EINVAL by SBI_NEED_FSCK when pinning a )
=======
int incfs_link(struct dentry *what, struct dentry *where);
int incfs_unlink(struct dentry *dentry);

static inline struct mount_info *get_mount_info(struct super_block *sb)
{
	struct mount_info *result = sb->s_fs_info;

	WARN_ON(!result);
	return result;
}

static inline struct super_block *file_superblock(struct file *f)
{
	struct inode *inode = file_inode(f);

	return inode->i_sb;
}
>>>>>>> CHANGE (d107cd Revert "ANDROID: incremental-fs: fix mount_fs issue")

#endif
