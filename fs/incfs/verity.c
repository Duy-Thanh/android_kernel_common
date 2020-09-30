// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020 Google LLC
 */

#include <crypto/hash.h>
#include <crypto/sha.h>
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
 * fsverity_info - cached verity metadata for an inode
 *
 * When a verity file is first opened, an instance of this struct is allocated
 * and stored in ->i_verity_info; it remains until the inode is evicted. It
 * caches the file measurement.
 */
struct fsverity_info {
	u8 measurement[FS_VERITY_MAX_DIGEST_SIZE];
	size_t measurement_size;
};

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

static int incfs_end_enable_verity(struct file *filp, const void *desc,
				   size_t desc_size)
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

/*
 * Merkle tree properties.  The file measurement is the hash of this structure
 * excluding the signature and with the sig_size field set to 0.
 */
struct fsverity_descriptor {
	__u8 version;		/* must be 1 */
	__u8 hash_algorithm;	/* Merkle tree hash algorithm */
	__u8 log_blocksize;	/* log2 of size of data and tree blocks */
	__u8 salt_size;		/* size of salt in bytes; 0 if none */
	__le32 sig_size;	/* size of signature in bytes; 0 if none */
	__le64 data_size;	/* size of file the Merkle tree is built over */
	__u8 root_hash[64];	/* Merkle tree root hash */
	__u8 salt[32];		/* salt prepended to each hashed block */
	__u8 __reserved[144];	/* must be 0's */
	__u8 signature[];	/* optional PKCS#7 signature */
} __packed;

/* Arbitrary limit to bound the kmalloc() size.  Can be changed. */
#define FS_VERITY_MAX_DESCRIPTOR_SIZE	16384

#define FS_VERITY_MAX_SIGNATURE_SIZE	(FS_VERITY_MAX_DESCRIPTOR_SIZE - \
					 sizeof(struct fsverity_descriptor))

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

	/* TODO: better derivation of hash alg */
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
		pr_debug("Paul\n");
		fsverity_set_info(inode, vi);
	}
out:
	kfree(desc);
	return err;
}

static int build_merkle_tree(struct file *f, struct data_file *df,
			     struct backing_file_context *bfc,
			     struct mtree *hash_tree, loff_t hash_offset,
			     struct incfs_hash_alg *alg, struct mem_range hash)
{
	int error = 0;
	struct mem_range buf = {.len = INCFS_DATA_FILE_BLOCK_SIZE};
	struct mem_range tmp = {.len = 2 * INCFS_DATA_FILE_BLOCK_SIZE};
	int limit, lvl, i, result;

	buf.data = (u8 *)__get_free_pages(GFP_NOFS, get_order(buf.len));
	tmp.data = (u8 *)__get_free_pages(GFP_NOFS, get_order(tmp.len));
	if (!buf.data || !tmp.data) {
		error = -ENOMEM;
		goto out;
	}

	/*
	 * lvl - 1 is the level we are reading, lvl the level we are writing
	 * lvl == -1 means actual blocks
	 * lvl == hash_tree->depth means root hash
	 */
	limit = df->df_data_block_count;
	for (lvl = 0; lvl <= hash_tree->depth; lvl++) {
		for (i = 0; i < limit; ++i) {
			loff_t hash_level;

			buf.len = INCFS_DATA_FILE_BLOCK_SIZE;
			if (lvl == 0)
				result = incfs_read_data_file_block(buf, f,
							    i, 0, 0, 0, tmp);
			else {
				hash_level = hash_offset +
				       hash_tree->hash_level_suboffset[lvl - 1];

				result = incfs_kread(bfc->bc_file, buf.data,
						buf.len, hash_level + i *
						    INCFS_DATA_FILE_BLOCK_SIZE);
			}

			if (result < 0) {
				error = result;
				goto out;
			}

			buf.len = result;
			error = incfs_calc_digest(alg, buf, hash);
			if (error)
				goto out;

			/*
			 * last level - only one hash to take and it is stored
			 * in the signature
			 */
			if (lvl == hash_tree->depth)
				break;

			hash_level = hash_offset +
				hash_tree->hash_level_suboffset[lvl];

			result = incfs_kwrite(bfc->bc_file, hash.data, hash.len,
					hash_level + hash.len * i);

			if (result < 0) {
				error = result;
				goto out;
			}

			if (result != hash.len) {
				error = -EIO;
				goto out;
			}
		}
		limit = DIV_ROUND_UP(limit,
				     INCFS_DATA_FILE_BLOCK_SIZE / hash.len);
	}

out:
	free_pages((unsigned long)tmp.data, get_order(tmp.len));
	free_pages((unsigned long)buf.data, get_order(buf.len));
	return error;
}

static int sign_file(struct file *f)
{
	/* See incfs_parse_signature */
	struct {
		u32 version;
		u32 size_of_hash_info_section;
		struct {
			u32 hash_algorithm;
			u8 log2_blocksize;
			u32 salt_size;
			u8 salt[0];
			u32 hash_size;
			u8 root_hash[32];
		} __packed hash_section;
		u32 size_of_signing_info_section;
		u8 signing_info_section[0];
	} __packed sig = {
		.version = INCFS_SIGNATURE_VERSION,
		.size_of_hash_info_section = sizeof(sig.hash_section),
		.hash_section = {
			.hash_algorithm = INCFS_HASH_TREE_SHA256,
			.log2_blocksize = 12,
			.hash_size = 32,
		},
	};

	struct data_file *df = get_incfs_data_file(f);
	struct mtree *hash_tree = NULL;
	struct backing_file_context *bfc;
	int error;
	loff_t hash_offset, sig_offset;
	struct incfs_hash_alg *alg = incfs_get_hash_alg(INCFS_HASH_TREE_SHA256);
	u8 hash_buf[INCFS_MAX_HASH_SIZE];
	int hash_size = alg->digest_size;
	struct mem_range hash = range(hash_buf, hash_size);
	int result;
	struct incfs_df_signature *signature = NULL;

	if (!df)
		return -EINVAL;

	if (df->df_header_flags & INCFS_FILE_MAPPED)
		return -EINVAL;

	error = mutex_lock_interruptible(&df->df_enable_verity);
	if (error)
		return error;

	/* Already signed? */
	if (df->df_signature && df->df_hash_tree)
		goto out;

	if (df->df_signature || df->df_hash_tree) {
		error = -EFAULT;
		goto out;
	}

	/* Add signature metadata record to file */
	hash_tree = incfs_alloc_mtree(range((u8 *)&sig, sizeof(sig)),
				      df->df_data_block_count);
	if (IS_ERR(hash_tree)) {
		error = PTR_ERR(hash_tree);
		goto out;
	}

	bfc = df->df_backing_file_context;
	if (!bfc)
		goto out;

	error = mutex_lock_interruptible(&bfc->bc_mutex);
	if (error)
		goto out;

	error = incfs_write_signature_to_backing_file(bfc,
				range((u8 *)&sig, sizeof(sig)),
				hash_tree->hash_tree_area_size,
				&hash_offset, &sig_offset);
	mutex_unlock(&bfc->bc_mutex);

	/* Populate merkle tree */
	error = build_merkle_tree(f, df, bfc, hash_tree, hash_offset, alg,
				  hash);
	if (error)
		goto out;

	/* Update signature metadata record */
	memcpy(sig.hash_section.root_hash, hash.data, alg->digest_size);
	result = incfs_kwrite(bfc->bc_file, &sig, sizeof(sig), sig_offset);
	if (result < 0) {
		error = result;
		goto out;
	}

	if (result != sizeof(sig)) {
		error = -EIO;
		goto out;
	}

	/* Update in-memory records */
	memcpy(hash_tree->root_hash, hash.data, alg->digest_size);
	signature = kzalloc(sizeof(*signature), GFP_NOFS);
	if (!signature) {
		error = -ENOMEM;
		goto out;
	}
	*signature = (struct incfs_df_signature) {
		.hash_offset = hash_offset,
		.hash_size = hash_tree->hash_tree_area_size,
		.sig_offset = sig_offset,
		.sig_size = sizeof(sig),
	};
	df->df_signature = signature;
	signature = NULL;

	/*
	 * Use memory barrier to prevent readpage seeing the hash tree until
	 * it's fully there
	 */
	smp_store_release(&df->df_hash_tree, hash_tree);
	hash_tree = NULL;

out:
	mutex_unlock(&df->df_enable_verity);
	kfree(signature);
	kfree(hash_tree);
	return error;
}

int ioctl_enable_verity(struct file *filp, const void __user *uarg)
{
	struct inode *inode = file_inode(filp);
	struct fsverity_enable_arg arg;
	int err;

	err = sign_file(filp);
	if (err)
		return err;

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

	/*
	 * Require a regular file with write access.  But the actual fd must
	 * still be readonly so that we can lock out all writers.  This is
	 * needed to guarantee that no writable fds exist to the file once it
	 * has verity enabled, and to stabilize the data being hashed.
	 */

	err = inode_permission(inode, MAY_WRITE);
	if (err)
		return err;

	if (IS_APPEND(inode))
		return -EPERM;

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	if (!S_ISREG(inode->i_mode))
		return -EINVAL;

	err = mnt_want_write_file(filp);
	if (err) /* -EROFS */
		return err;

	err = deny_write_access(filp);
	if (err) /* -ETXTBSY */
		goto out_drop_write;

	err = enable_verity(filp, &arg);
	if (err)
		goto out_allow_write_access;

	/*
	 * allow_write_access() is needed to pair with deny_write_access().
	 * Regardless, the filesystem won't allow writing to verity files.
	 */
out_allow_write_access:
	allow_write_access(filp);
out_drop_write:
	mnt_drop_write_file(filp);
	return err;
}

int incfs_verity_get_flags(struct file *f, void __user *arg)
{
	u32 flags = (file_inode(f)->i_flags & S_VERITY) ? FS_VERITY_FL : 0;

	return put_user(flags, (int __user *) arg);
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

/* Ensure the inode has an ->i_verity_info */
static int ensure_verity_info(struct inode *inode, struct file *filp)
{
	struct fsverity_info *vi = fsverity_get_info(inode);
	struct fsverity_descriptor *desc;
	int res;
	const struct fsverity_enable_arg arg = {
		.hash_algorithm = FS_VERITY_HASH_ALG_SHA256,
		.block_size = INCFS_DATA_FILE_BLOCK_SIZE,
	};
	u8 root_hash[FS_VERITY_MAX_DIGEST_SIZE] = {};

	if (vi)
		return 0;

	res = incfs_get_root_hash(filp, &arg, root_hash);
	if (res) {
		pr_err("Failed to get root hash %d", res);
		goto out_free_desc;
	}

	res = incfs_get_verity_descriptor(inode, NULL, 0);
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
	res = incfs_get_verity_descriptor(inode, desc, res);
	if (res < 0) {
		pr_err("Error %d reading verity descriptor", res);
		goto out_free_desc;
	}

	if (memcmp(root_hash, desc->root_hash, sizeof(root_hash))) {
		pr_err("Root hashes do not match");
		res = -EINVAL;
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
