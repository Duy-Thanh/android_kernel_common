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
 * fs-verity integration into incfs
 *
 * Since incfs has its own merkle tree implementation, most of fs-verity code
 * is not needed. The key part that is needed is the signature check, since
 * that is based on the private /proc/sys/fs/verity/require_signatures value
 * and a private keyring. Thus the first change is to modify verity code to
 * export a version of fsverity_verify_signature.
 *
 * fs-verity integration then consists of the following modifications:
 *
 * 1. Add the (optional) verity signature to the incfs file format
 * 2. Add a pointer to the digest of the fs-verity descriptor struct to the
 *    data_file struct that incfs attaches to each file inode.
 * 3. Add the following ioclts:
 *  - FS_IOC_ENABLE_VERITY
 *  - FS_IOC_GETFLAGS
 *  - FS_IOC_MEASURE_VERITY
 * 4. When FS_IOC_ENABLE_VERITY is called on a non-verity file, the
 *    fs-verity descriptor struct is populated and digested. If it passes the
 *    signature check, then the S_VERITY flag is set, the digest is added to the
 *    data_file struct and the signature is added to the backing file.
 * 5. When a file with a verity signature is opened, the inode’s S_VERITY flag
 *    is set. If the file doesn’t have a digest, the file’s digest is calculated
 *    as above, checked, and set.
 * 6. FS_IOC_GETFLAGS simply returns the value of the S_VERITY flag
 * 7. FS_IOC_MEASURE_VERITY simply returns the stored digest
 * 8. The final complication is that if FS_IOC_ENABLE_VERITY is called on a file
 *    which doesn’t have a merkle tree, the merkle tree is calculated before the
 *    rest of the process is completed.
 */

/* Largest digest size among all hash algorithms supported by fs-verity. */
#define FS_VERITY_MAX_DIGEST_SIZE	SHA512_DIGEST_SIZE

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

static int incfs_end_enable_verity(struct file *filp, u8 *sig, size_t sig_size)
{
	struct inode *inode = file_inode(filp);
	struct mem_range signature = {
		.data = sig,
		.len = sig_size,
	};
	struct data_file *df = get_incfs_data_file(filp);
	struct backing_file_context *bfc;
	int error;
	struct incfs_df_verity_signature *vs;
	loff_t offset;

	if (!df || !df->df_backing_file_context)
		return -EFSCORRUPTED;

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
	SHASH_DESC_ON_STACK(d, alg->shash);

	d->tfm = alg->shash;
	return crypto_shash_digest(d, (u8 *)desc, sizeof(*desc), measurement);
}

/*
 * Validate the given fsverity_descriptor and calculate its digest. The
 * signature (if present) is also checked.
 */
static struct mem_range fsverity_calc_digest(const struct inode *inode,
					     struct fsverity_descriptor *desc,
					     u8 *signature, size_t sig_size)
{
	struct mem_range verity_file_digest;
	int err;
	struct incfs_hash_alg *hash_alg;

	hash_alg = incfs_get_hash_alg(desc->hash_algorithm);
	if (IS_ERR(hash_alg))
		return range((u8 *)hash_alg, 0);

	verity_file_digest = range(kzalloc(FS_VERITY_MAX_DIGEST_SIZE,
					   GFP_KERNEL), hash_alg->digest_size);
	if (!verity_file_digest.data)
		return range(ERR_PTR(-ENOMEM), 0);

	err = compute_file_measurement(hash_alg, desc, verity_file_digest.data);
	if (err) {
		pr_err("Error %d computing file measurement", err);
		goto out;
	}
	pr_debug("Computed file measurement: %s:%*phN\n",
		 hash_alg->name, (int) verity_file_digest.len,
		 verity_file_digest.data);

	err = __fsverity_verify_signature(inode, desc->signature,
					  le32_to_cpu(desc->sig_size),
					  verity_file_digest.data,
					  FS_VERITY_HASH_ALG_SHA256);
out:
	if (err) {
		kfree(verity_file_digest.data);
		verity_file_digest = range(ERR_PTR(err), 0);
	}
	return verity_file_digest;
}

static struct mem_range fsverity_get_file_digest(struct inode *inode)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df;
	struct mem_range verity_file_digest;

	if (!node) {
		pr_warn("Invalid inode\n");
		return range(NULL, 0);
	}

	df = node->n_file;

	/*
	 * Pairs with the cmpxchg_release() in fsverity_set_file_digest().
	 * I.e., another task may publish ->df_verity_file_digest concurrently,
	 * executing a RELEASE barrier.  We need to use smp_load_acquire() here
	 * to safely ACQUIRE the memory the other task published.
	 */
	verity_file_digest.data = smp_load_acquire(
					&df->df_verity_file_digest.data);
	verity_file_digest.len = df->df_verity_file_digest.len;
	return verity_file_digest;
}

static void fsverity_set_file_digest(struct inode *inode,
				     struct mem_range verity_file_digest)
{
	struct inode_info *node = get_incfs_node(inode);
	struct data_file *df;

	if (!node) {
		pr_warn("Invalid inode\n");
		return;
	}

	df = node->n_file;
	df->df_verity_file_digest.len = verity_file_digest.len;

	/*
	 * Multiple tasks may race to set ->df_verity_file_digest.data, so use
	 * cmpxchg_release().  This pairs with the smp_load_acquire() in
	 * fsverity_get_file_digest().  I.e., here we publish
	 * ->df_verity_file_digest.data, with a RELEASE barrier so that other
	 * tasks can ACQUIRE it.
	 */
	if (cmpxchg_release(&df->df_verity_file_digest.data, NULL,
			    verity_file_digest.data) != NULL)
		/* Lost the race, so free the fsverity_info we allocated. */
		kfree(verity_file_digest.data);
}

static int enable_verity(struct file *filp,
			 const struct fsverity_enable_arg *arg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_descriptor *desc;
	u8 *signature = NULL;
	struct mem_range verity_file_digest;
	int err;

	/* Start initializing the fsverity_descriptor */
	desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->version = 1;
	desc->hash_algorithm = arg->hash_algorithm;
	desc->log_blocksize = ilog2(arg->block_size);
	desc->data_size = cpu_to_le64(inode->i_size);

	/* Get the signature if the user provided one */
	if (arg->sig_size) {
		signature = kzalloc(arg->sig_size, GFP_KERNEL);
		if (!signature) {
			err = -ENOMEM;
			goto out;
		}

		if (copy_from_user(signature, u64_to_user_ptr(arg->sig_ptr),
				   arg->sig_size)) {
			err = -EFAULT;
			goto out;
		}
	}

	err = incfs_get_root_hash(filp, arg, desc->root_hash);
	if (err)
		goto out;

	verity_file_digest = fsverity_calc_digest(inode, desc,
						  signature, arg->sig_size);
	if (IS_ERR(verity_file_digest.data)) {
		err = PTR_ERR(verity_file_digest.data);
		verity_file_digest.data = NULL;
		goto out;
	}

	err = incfs_end_enable_verity(filp, signature, arg->sig_size);
	if (err)
		goto out;

	if (WARN_ON(!IS_VERITY(inode))) {
		err = -EINVAL;
		goto out;
	}

	/* Successfully enabled verity */
	fsverity_set_file_digest(inode, verity_file_digest);
	verity_file_digest.data = NULL;
out:
	kfree(signature);
	kfree(desc);
	kfree(verity_file_digest.data);
	if (err)
		pr_err("failed with err %d", err);
	return err;
}

int incfs_ioctl_enable_verity(struct file *filp, const void __user *uarg)
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

static struct fsverity_descriptor *incfs_get_verity_descriptor(
					struct inode *inode, struct file *filp)
{
	const struct fsverity_enable_arg arg = {
		.hash_algorithm = FS_VERITY_HASH_ALG_SHA256,
		.block_size = INCFS_DATA_FILE_BLOCK_SIZE,
	};
	struct fsverity_descriptor *desc = kzalloc(sizeof(*desc), GFP_KERNEL);
	int res;

	if (!desc)
		return ERR_PTR(-ENOMEM);

	*desc = (struct fsverity_descriptor) {
		.version = 1,
		.hash_algorithm = FS_VERITY_HASH_ALG_SHA256,
		.log_blocksize = ilog2(INCFS_DATA_FILE_BLOCK_SIZE),
		.data_size = cpu_to_le64(inode->i_size),
	};

	res = incfs_get_root_hash(filp, &arg, desc->root_hash);
	if (res) {
		kfree(desc);
		return ERR_PTR(res);
	}

	return desc;
}

static u8 *incfs_get_verity_signature(struct file *filp, size_t *sig_size)
{
	struct data_file *df = get_incfs_data_file(filp);
	struct incfs_df_verity_signature *vs;
	u8 *signature;
	int res;

	if (!df || !df->df_backing_file_context)
		return ERR_PTR(-EFSCORRUPTED);

	vs = df->df_verity_signature;
	if (!vs)
		return ERR_PTR(-EINVAL);

	signature = kzalloc(vs->size, GFP_KERNEL);
	if (!signature)
		return ERR_PTR(-ENOMEM);

	res = incfs_kread(df->df_backing_file_context,
			  signature, vs->size, vs->offset);

	if (res < 0)
		goto err_out;

	if (res != vs->size) {
		res = -EINVAL;
		goto err_out;
	}

	*sig_size = vs->size;
	return signature;

err_out:
	kfree(signature);
	return ERR_PTR(res);

}

/* Ensure data_file->df_verity_file_digest is populated */
static int ensure_verity_info(struct inode *inode, struct file *filp)
{
	struct mem_range verity_file_digest = fsverity_get_file_digest(inode);
	struct fsverity_descriptor *desc;
	u8 *signature = NULL;
	size_t sig_size;
	int err;

	if (verity_file_digest.data)
		return 0;

	desc = incfs_get_verity_descriptor(inode, filp);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	signature = incfs_get_verity_signature(filp, &sig_size);
	if (IS_ERR(signature)) {
		err = PTR_ERR(signature);
		signature = NULL;
		goto out;
	}

	verity_file_digest = fsverity_calc_digest(inode, desc, signature,
						  sig_size);
	if (IS_ERR(verity_file_digest.data)) {
		err = PTR_ERR(verity_file_digest.data);
		goto out;
	}

	fsverity_set_file_digest(inode, verity_file_digest);
	err = 0;

out:
	kfree(desc);
	kfree(signature);
	return err;
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
	struct data_file *df = get_incfs_data_file(filp);

	if (!df || !df->df_verity_signature)
		return 0;

	if (filp->f_mode & FMODE_WRITE) {
		pr_debug("Denying opening verity file (ino %lu) for write\n",
			 inode->i_ino);
		return -EPERM;
	}

	inode_set_flags(inode, S_VERITY, S_VERITY);
	return ensure_verity_info(inode, filp);
}

int incfs_ioctl_verity_measure(struct file *filp, void __user *_uarg)
{
	struct inode *inode = file_inode(filp);
	struct mem_range verity_file_digest = fsverity_get_file_digest(inode);
	struct fsverity_digest __user *uarg = _uarg;
	struct fsverity_digest arg;

	if (!verity_file_digest.data || !verity_file_digest.len)
		return -ENODATA; /* not a verity file */

	/*
	 * The user specifies the digest_size their buffer has space for; we can
	 * return the digest if it fits in the available space.  We write back
	 * the actual size, which may be shorter than the user-specified size.
	 */

	if (get_user(arg.digest_size, &uarg->digest_size))
		return -EFAULT;
	if (arg.digest_size < verity_file_digest.len)
		return -EOVERFLOW;

	memset(&arg, 0, sizeof(arg));
	arg.digest_algorithm = 1; /* TODO - don't hard code */
	arg.digest_size = verity_file_digest.len;

	if (copy_to_user(uarg, &arg, sizeof(arg)))
		return -EFAULT;

	if (copy_to_user(uarg->digest, verity_file_digest.data,
			 verity_file_digest.len))
		return -EFAULT;

	return 0;
}
