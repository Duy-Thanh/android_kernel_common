// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <crypto/hash.h>
#include <crypto/sha2.h>
#include <linux/fsverity.h>
#include <linux/mount.h>

#include "verity.h"

#include "data_mgmt.h"
#include "format.h"
#include "integrity.h"
#include "vfs.h"

/*
 * Largest digest size among all hash algorithms supported by fs-verity.
 */
#define FS_VERITY_MAX_DIGEST_SIZE	SHA512_DIGEST_SIZE

/*
 * fsverity_info - cached verity measurement for an inode, stored in
 * ->i_verity_info
 *
 * This can be set in one of two ways.
 *
 * When an incfs inode is opened, the file's metadata sections are loaded. If
 * one of them is a verity signature, the inode has the S_VERITY flag set and
 * the incfs data_file struct has the ->df_verity_signature field populated.
 *
 * Then when the inode is opened as a file, an fsverity_info struct is created,
 * validated and ->i_verity_info is set to point to it. For subsequent opens,
 * this field will be present and the open can simply proceed.
 *
 * Alternatively if there is no verity signature, the file is not a verity file.
 * When the ioctl FS_IOC_ENABLE_VERITY is called on the file, an fsverity_info
 * struct is created, validated and ->i_verity_info is set to point to it.
 *
 * Either way, ->i_verity_info now remains until the inode is evicted.
 */
struct fsverity_info {
	u8 measurement[FS_VERITY_MAX_DIGEST_SIZE];
	size_t measurement_size;
};

/* Arbitrary limit to bound the kmalloc() size.  Can be changed. */
#define FS_VERITY_MAX_DESCRIPTOR_SIZE	16384

#define FS_VERITY_MAX_SIGNATURE_SIZE	(FS_VERITY_MAX_DESCRIPTOR_SIZE - \
					 sizeof(struct fsverity_descriptor))

static int incfs_get_root_hash(struct file *filp,
			       const struct fsverity_enable_arg *arg,
			       u8 *root_hash)
{
	struct data_file *df = get_incfs_data_file(filp);

	if (!df)
		return -EINVAL;

	if (arg->hash_algorithm != FS_VERITY_HASH_ALG_SHA256 ||
	    arg->block_size != INCFS_DATA_FILE_BLOCK_SIZE ||
	    arg->salt_size != 0)
		return -EINVAL;

	memcpy(root_hash, df->df_hash_tree->root_hash,
	       df->df_hash_tree->alg->digest_size);

	return 0;
}

static int incfs_end_enable_verity(struct file *filp,
				   struct fsverity_descriptor *desc,
				   size_t desc_size)
{
	struct inode *inode = file_inode(filp);
	struct mem_range signature = {
		.data = (void *) desc->signature,
		.len = desc_size - sizeof(*desc),
	};
	struct data_file *df = get_incfs_data_file(filp);
	struct backing_file_context *bfc;
	int error;
	struct incfs_df_verity_signature *vs;
	loff_t offset;

	if (!desc)
		return 0;

	if (!df || !df->df_backing_file_context)
		return -EFAULT;

	bfc = df->df_backing_file_context;
	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (error)
		return error;
	error = incfs_write_verity_signature_to_backing_file(bfc, signature,
							     &offset);
	mutex_unlock(&bfc->bc_mutex);
	if (error)
		return error;

	vs = kzalloc(sizeof(*vs), GFP_NOFS);
	if (!vs)
		return -ENOMEM;

	*vs = (struct incfs_df_verity_signature) {
		.size = signature.len,
		.offset = offset,
	};

	df->df_verity_signature = vs;
	inode_set_flags(inode, S_VERITY, S_VERITY);

	return 0;
}

static int compute_file_measurement(struct incfs_hash_alg *alg,
				    struct fsverity_descriptor *desc,
				    u8 *measurement)
{
	__le32 sig_size = desc->sig_size;
	int err;
	SHASH_DESC_ON_STACK(d, alg->shash);

	d->tfm = alg->shash;
	desc->sig_size = 0;
	err = crypto_shash_digest(d, (u8 *)desc, sizeof(*desc), measurement);
	desc->sig_size = sig_size;

	return err;
}

/*
 * Validate the given fsverity_descriptor and create a new fsverity_info from
 * it.  The signature (if present) is also checked.
 */
static struct fsverity_info *fsverity_create_info(const struct inode *inode,
					   void *_desc, size_t desc_size)
{
	struct fsverity_descriptor *desc = _desc;
	struct fsverity_info *vi;
	int err;
	struct incfs_hash_alg *hash_alg;

	if (desc_size < sizeof(*desc)) {
		pr_err("Unrecognized descriptor size: %zu bytes", desc_size);
		return ERR_PTR(-EINVAL);
	}

	if (desc->version != 1) {
		pr_err("Unrecognized descriptor version: %u", desc->version);
		return ERR_PTR(-EINVAL);
	}

	if (memchr_inv(desc->__reserved, 0, sizeof(desc->__reserved))) {
		pr_err("Reserved bits set in descriptor");
		return ERR_PTR(-EINVAL);
	}

	if (desc->salt_size > sizeof(desc->salt)) {
		pr_err("Invalid salt_size: %u", desc->salt_size);
		return ERR_PTR(-EINVAL);
	}

	if (le64_to_cpu(desc->data_size) != inode->i_size) {
		pr_err("Wrong data_size: %llu (desc) != %lld (inode)",
		       le64_to_cpu(desc->data_size), inode->i_size);
		return ERR_PTR(-EINVAL);
	}

	vi = kzalloc(sizeof(*vi), GFP_KERNEL);
	if (!vi)
		return ERR_PTR(-ENOMEM);

	hash_alg = incfs_get_hash_alg(desc->hash_algorithm);
	if (IS_ERR(hash_alg))
		return (struct fsverity_info *)hash_alg;

	err = compute_file_measurement(hash_alg, desc, vi->measurement);
	if (err) {
		pr_err("Error %d computing file measurement", err);
		goto out;
	}
	pr_debug("Computed file measurement: %s:%*phN\n",
		 hash_alg->name, hash_alg->digest_size, vi->measurement);
	vi->measurement_size = hash_alg->digest_size;

	err = __fsverity_verify_signature(inode, desc->signature,
					  le32_to_cpu(desc->sig_size),
					  vi->measurement,
					  FS_VERITY_HASH_ALG_SHA256);
out:
	if (err) {
		kfree(vi);
		vi = ERR_PTR(err);
	}
	return vi;
}

static void fsverity_set_info(struct inode *inode, struct fsverity_info *vi)
{
	/*
	 * Multiple tasks may race to set ->i_verity_info, so use
	 * cmpxchg_release().  This pairs with the smp_load_acquire() in
	 * fsverity_get_info().  I.e., here we publish ->i_verity_info with a
	 * RELEASE barrier so that other tasks can ACQUIRE it.
	 */
	if (cmpxchg_release(&inode->i_verity_info, NULL, vi) != NULL) {
		/* Lost the race, so free the fsverity_info we allocated. */
		kfree(vi);
		/*
		 * Afterwards, the caller may access ->i_verity_info directly,
		 * so make sure to ACQUIRE the winning fsverity_info.
		 */
		(void)fsverity_get_info(inode);
	}
}

static int enable_verity(struct file *filp,
			 const struct fsverity_enable_arg *arg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_descriptor *desc;
	size_t desc_size = sizeof(*desc) + arg->sig_size;
	struct fsverity_info *vi;
	int err;

	/* Start initializing the fsverity_descriptor */
	desc = kzalloc(desc_size, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	desc->version = 1;
	desc->hash_algorithm = arg->hash_algorithm;
	desc->log_blocksize = ilog2(arg->block_size);

	/* Get the signature if the user provided one */
	if (arg->sig_size &&
	    copy_from_user(desc->signature, u64_to_user_ptr(arg->sig_ptr),
			   arg->sig_size)) {
		err = -EFAULT;
		goto out;
	}
	desc->sig_size = cpu_to_le32(arg->sig_size);

	desc->data_size = cpu_to_le64(inode->i_size);

	/*
	 * Start enabling verity on this file, serialized by the inode lock.
	 * Fail if verity is already enabled or is already being enabled.
	 */
	inode_lock(inode);
	err = IS_VERITY(inode) ? -EEXIST : 0;
	inode_unlock(inode);
	if (err)
		goto out;

	err = incfs_get_root_hash(filp, arg, desc->root_hash);
	if (err)
		goto out;

	vi = fsverity_create_info(inode, desc, desc_size);
	if (IS_ERR(vi)) {
		err = PTR_ERR(vi);
		goto out;
	}

	/*
	 * Tell the filesystem to finish enabling verity on the file.
	 * Serialized with ->begin_enable_verity() by the inode lock.
	 */
	inode_lock(inode);
	err = incfs_end_enable_verity(filp, desc, desc_size);
	inode_unlock(inode);
	if (err) {
		pr_err("failed with err %d", err);
		kfree(vi);
	} else if (WARN_ON(!IS_VERITY(inode))) {
		err = -EINVAL;
		kfree(vi);
	} else {
		/* Successfully enabled verity */

		/*
		 * Readers can start using ->i_verity_info immediately, so it
		 * can't be rolled back once set.  So don't set it until just
		 * after the filesystem has successfully enabled verity.
		 */
		fsverity_set_info(inode, vi);
	}
out:
	kfree(desc);
	return err;
}

int ioctl_enable_verity(struct file *filp, const void __user *uarg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_enable_arg arg;

	if (copy_from_user(&arg, uarg, sizeof(arg)))
		return -EFAULT;

	if (arg.version != 1)
		return -EINVAL;

	if (arg.__reserved1 ||
	    memchr_inv(arg.__reserved2, 0, sizeof(arg.__reserved2)))
		return -EINVAL;

	if (arg.block_size != PAGE_SIZE)
		return -EINVAL;

	if (arg.salt_size)
		return -EINVAL;

	if (arg.sig_size > FS_VERITY_MAX_SIGNATURE_SIZE)
		return -EMSGSIZE;

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	return enable_verity(filp, &arg);
}

int incfs_verity_get_flags(struct file *f, void __user *arg)
{
	u32 flags = (file_inode(f)->i_flags & S_VERITY) ? FS_VERITY_FL : 0;

	return put_user(flags, (int __user *) arg);
}

static int incfs_get_verity_descriptor(struct inode *inode, struct file *filp,
				       void *buf, size_t buf_size)
{
	struct data_file *df = get_incfs_data_file(filp);
	struct incfs_df_verity_signature *vs;
	struct fsverity_descriptor *desc = buf;
	size_t desc_size;
	const struct fsverity_enable_arg arg = {
		.hash_algorithm = FS_VERITY_HASH_ALG_SHA256,
		.block_size = INCFS_DATA_FILE_BLOCK_SIZE,
	};
	int res;

	if (!df || !df->df_backing_file_context)
		return -EFAULT;

	vs = df->df_verity_signature;
	if (!vs)
		return 0;

	desc_size = sizeof(*desc) + vs->size;

	if (!buf_size)
		return desc_size;

	if (desc_size > buf_size)
		return -ERANGE;

	*desc = (struct fsverity_descriptor) {
		.version = 1,
		.hash_algorithm = FS_VERITY_HASH_ALG_SHA256,
		.log_blocksize = ilog2(INCFS_DATA_FILE_BLOCK_SIZE),
		.sig_size = cpu_to_le32(vs->size),
		.data_size = cpu_to_le64(inode->i_size),
	};

	res = incfs_get_root_hash(filp, &arg, desc->root_hash);
	if (res) {
		pr_err("Failed to get root hash %d", res);
		return res;
	}

	res = incfs_kread(df->df_backing_file_context,
			  desc->signature, vs->size, vs->offset);

	if (res < 0)
		return res;

	if (res != vs->size)
		return -EINVAL;

	return res;
}

/* Ensure the inode has an ->i_verity_info */
static int ensure_verity_info(struct inode *inode, struct file *filp)
{
	struct fsverity_info *vi = fsverity_get_info(inode);
	struct fsverity_descriptor *desc;
	int res;

	if (vi)
		return 0;

	res = incfs_get_verity_descriptor(inode, filp, NULL, 0);
	if (res < 0) {
		pr_err("Error %d getting verity descriptor size", res);
		return res;
	}
	if (res > FS_VERITY_MAX_DESCRIPTOR_SIZE) {
		pr_err("Verity descriptor is too large (%d bytes)",
			     res);
		return -EMSGSIZE;
	}
	desc = kmalloc(res, GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	res = incfs_get_verity_descriptor(inode, filp, desc, res);
	if (res < 0) {
		pr_err("Error %d reading verity descriptor", res);
		goto out_free_desc;
	}

	vi = fsverity_create_info(inode, desc, res);
	if (IS_ERR(vi)) {
		res = PTR_ERR(vi);
		goto out_free_desc;
	}

	fsverity_set_info(inode, vi);
	res = 0;
out_free_desc:
	kfree(desc);
	return res;
}

/**
 * fsverity_file_open() - prepare to open a verity file
 * @inode: the inode being opened
 * @filp: the struct file being set up
 *
 * When opening a verity file, deny the open if it is for writing.  Otherwise,
 * set up the inode's ->i_verity_info if not already done.
 *
 * When combined with fscrypt, this must be called after fscrypt_file_open().
 * Otherwise, we won't have the key set up to decrypt the verity metadata.
 *
 * Return: 0 on success, -errno on failure
 */
int incfs_fsverity_file_open(struct inode *inode, struct file *filp)
{
	if (!IS_VERITY(inode))
		return 0;

	if (filp->f_mode & FMODE_WRITE) {
		pr_debug("Denying opening verity file (ino %lu) for write\n",
			 inode->i_ino);
		return -EPERM;
	}

	return ensure_verity_info(inode, filp);
}

int incfs_verity_measure(struct file *filp, void __user *_uarg)
{
	const struct inode *inode = file_inode(filp);
	struct fsverity_digest __user *uarg = _uarg;
	const struct fsverity_info *vi;
	struct fsverity_digest arg;

	vi = fsverity_get_info(inode);
	if (!vi)
		return -ENODATA; /* not a verity file */

	/*
	 * The user specifies the digest_size their buffer has space for; we can
	 * return the digest if it fits in the available space.  We write back
	 * the actual size, which may be shorter than the user-specified size.
	 */

	if (get_user(arg.digest_size, &uarg->digest_size))
		return -EFAULT;
	if (arg.digest_size < vi->measurement_size)
		return -EOVERFLOW;

	memset(&arg, 0, sizeof(arg));
	arg.digest_algorithm = 1; /* TODO - don't hard code */
	arg.digest_size = vi->measurement_size;

	if (copy_to_user(uarg, &arg, sizeof(arg)))
		return -EFAULT;

	if (copy_to_user(uarg->digest, vi->measurement, vi->measurement_size))
		return -EFAULT;

	return 0;
}
