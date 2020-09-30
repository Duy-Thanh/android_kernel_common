// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <linux/fsverity.h>

#include "data_mgmt.h"
#include "format.h"
#include "integrity.h"
#include "vfs.h"

static int incfs_begin_enable_verity(struct file *filp)
{
	return 0;
}

static int incfs_end_enable_verity(struct file *filp, const void *desc,
				   size_t desc_size, u64 merkle_tree_size)
{
	struct inode *inode = file_inode(filp);
	struct mem_range descriptor = {
		.data = (void *) desc,
		.len = desc_size,
	};
	struct data_file *df = get_incfs_data_file(filp);
	struct backing_file_context *bfc;
	int error;
	struct incfs_df_verity_descriptor *vd;
	loff_t offset;

	if (!desc)
		return 0;

	if (!df || !df->df_backing_file_context)
		return -EFAULT;

	bfc = df->df_backing_file_context;
	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (error)
		return error;
	error = incfs_write_verity_descriptor_to_backing_file(bfc, descriptor,
							       &offset);
	mutex_unlock(&bfc->bc_mutex);
	if (error)
		return error;

	vd = kzalloc(sizeof(*vd), GFP_NOFS);
	if (!vd)
		return -ENOMEM;

	*vd = (struct incfs_df_verity_descriptor) {
		.size = desc_size,
		.offset = offset,
	};

	df->df_verity_descriptor = vd;
	inode->i_private = df;
	inode_set_flags(inode, S_VERITY, S_VERITY);

	return 0;
}

static int incfs_get_verity_descriptor(struct inode *inode, void *buf,
				       size_t buf_size)
{
	struct data_file *df = inode->i_private;
	struct incfs_df_verity_descriptor *vd;
	int read;

	if (!df || !df->df_backing_file_context)
		return -EFAULT;

	vd = df->df_verity_descriptor;
	if (!vd)
		return 0;

	if (!buf_size)
		return vd->size;

	if (vd->size > buf_size)
		return -ERANGE;

	read = incfs_kread(df->df_backing_file_context->bc_file, buf, vd->size,
		    vd->offset);

	if (read < 0)
		return read;

	if (read != vd->size)
		return -EINVAL;

	return read;
}

static int incfs_get_root_hash(struct file *filp,
			       const struct fsverity_enable_arg *arg,
			       u8 *root_hash)
{
	struct data_file *df = get_incfs_data_file(filp);
	struct mtree *hash_tree;

	if (!df)
		return -EINVAL;

	if (arg->hash_algorithm != FS_VERITY_HASH_ALG_SHA256 ||
	    arg->block_size != INCFS_DATA_FILE_BLOCK_SIZE ||
	    arg->salt_size != 0)
		return -EINVAL;
	/*
	 * Memory barrier to ensure hash tree fully present if being added via
	 * enable verity
	 */
	hash_tree = smp_load_acquire(&df->df_hash_tree);
	if (!hash_tree)
		return -EOPNOTSUPP;

	memcpy(root_hash, df->df_hash_tree->root_hash,
	       df->df_hash_tree->alg->digest_size);

	return 0;
}

int incfs_verity_get_flags(struct file *f, void __user *arg)
{
	u32 flags = (file_inode(f)->i_flags & S_VERITY) ? FS_VERITY_FL : 0;

	return put_user(flags, (int __user *) arg);
}

const struct fsverity_operations incfs_verityops = {
	.begin_enable_verity		= incfs_begin_enable_verity,
	.end_enable_verity		= incfs_end_enable_verity,
	.get_verity_descriptor		= incfs_get_verity_descriptor,
	.get_root_hash			= incfs_get_root_hash,
};
