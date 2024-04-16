// SPDX-License-Identifier: GPL-2.0
<<<<<<< HEAD   (a365b2 Merge ca7e91776912 ("Merge tag 'x86-apic-2024-03-10' of git:)

#include "fuse_i.h"

#include <linux/file.h>
#include <linux/fuse.h>
#include <linux/idr.h>
#include <linux/uio.h>

#define PASSTHROUGH_IOCB_MASK                                                  \
	(IOCB_APPEND | IOCB_DSYNC | IOCB_HIPRI | IOCB_NOWAIT | IOCB_SYNC)

struct fuse_aio_req {
	struct kiocb iocb;
	struct kiocb *iocb_fuse;
};

static inline rwf_t fuse_iocb_to_rw_flags(int ifl, int iocb_mask)
{
	return ifl & iocb_mask;
}

static void fuse_file_accessed(struct file *dst_file, struct file *src_file)
{
	struct inode *dst_inode;
	struct inode *src_inode;
	struct timespec64 dst_ctime_ts;
	struct timespec64 src_ctime_ts;
	struct timespec64 dst_mtime_ts;
	struct timespec64 src_mtime_ts;

	if (dst_file->f_flags & O_NOATIME)
		return;

	dst_inode = file_inode(dst_file);
	src_inode = file_inode(src_file);
	dst_ctime_ts = inode_get_ctime(dst_inode);
	src_ctime_ts = inode_get_ctime(src_inode);
	dst_mtime_ts = inode_get_mtime(dst_inode);
	src_mtime_ts = inode_get_mtime(src_inode);


	if ((!timespec64_equal(&dst_mtime_ts, &src_mtime_ts) ||
	     !timespec64_equal(&dst_ctime_ts, &src_ctime_ts))) {
		inode_set_mtime_to_ts(dst_inode, inode_get_mtime(src_inode));
		inode_set_ctime_to_ts(dst_inode, inode_get_ctime(src_inode));
	}

	touch_atime(&dst_file->f_path);
}

static void fuse_copyattr(struct file *dst_file, struct file *src_file)
{
	struct inode *dst = file_inode(dst_file);
	struct inode *src = file_inode(src_file);

	inode_set_atime_to_ts(dst, inode_get_atime(src));
	inode_set_mtime_to_ts(dst, inode_get_mtime(src));
	inode_set_ctime_to_ts(dst, inode_get_ctime(src));
	i_size_write(dst, i_size_read(src));
}

static void fuse_aio_cleanup_handler(struct fuse_aio_req *aio_req)
{
	struct kiocb *iocb = &aio_req->iocb;
	struct kiocb *iocb_fuse = aio_req->iocb_fuse;

	if (iocb->ki_flags & IOCB_WRITE) {
		__sb_writers_acquired(file_inode(iocb->ki_filp)->i_sb,
				      SB_FREEZE_WRITE);
		file_end_write(iocb->ki_filp);
		fuse_copyattr(iocb_fuse->ki_filp, iocb->ki_filp);
	}

	iocb_fuse->ki_pos = iocb->ki_pos;
	kfree(aio_req);
}

static void fuse_aio_rw_complete(struct kiocb *iocb, long res)
{
	struct fuse_aio_req *aio_req =
		container_of(iocb, struct fuse_aio_req, iocb);
	struct kiocb *iocb_fuse = aio_req->iocb_fuse;

	fuse_aio_cleanup_handler(aio_req);
	iocb_fuse->ki_complete(iocb_fuse, res);
}

ssize_t fuse_passthrough_read_iter(struct kiocb *iocb_fuse,
				   struct iov_iter *iter)
{
	ssize_t ret;
	const struct cred *old_cred;
	struct file *fuse_filp = iocb_fuse->ki_filp;
	struct fuse_file *ff = fuse_filp->private_data;
	struct file *passthrough_filp = ff->passthrough.filp;

	if (!iov_iter_count(iter))
		return 0;

	old_cred = override_creds(ff->passthrough.cred);
	if (is_sync_kiocb(iocb_fuse)) {
		ret = vfs_iter_read(
			passthrough_filp, iter, &iocb_fuse->ki_pos,
			fuse_iocb_to_rw_flags(iocb_fuse->ki_flags,
					      PASSTHROUGH_IOCB_MASK));
	} else {
		struct fuse_aio_req *aio_req;

		aio_req = kmalloc(sizeof(struct fuse_aio_req), GFP_KERNEL);
		if (!aio_req) {
			ret = -ENOMEM;
			goto out;
		}

		aio_req->iocb_fuse = iocb_fuse;
		kiocb_clone(&aio_req->iocb, iocb_fuse, passthrough_filp);
		aio_req->iocb.ki_complete = fuse_aio_rw_complete;
		ret = call_read_iter(passthrough_filp, &aio_req->iocb, iter);
		if (ret != -EIOCBQUEUED)
			fuse_aio_cleanup_handler(aio_req);
	}
out:
	revert_creds(old_cred);

	fuse_file_accessed(fuse_filp, passthrough_filp);

	return ret;
}

ssize_t fuse_passthrough_write_iter(struct kiocb *iocb_fuse,
				    struct iov_iter *iter)
{
	ssize_t ret;
	const struct cred *old_cred;
	struct file *fuse_filp = iocb_fuse->ki_filp;
	struct fuse_file *ff = fuse_filp->private_data;
	struct inode *fuse_inode = file_inode(fuse_filp);
	struct file *passthrough_filp = ff->passthrough.filp;
	struct inode *passthrough_inode = file_inode(passthrough_filp);

	if (!iov_iter_count(iter))
		return 0;

	inode_lock(fuse_inode);

	fuse_copyattr(fuse_filp, passthrough_filp);

	old_cred = override_creds(ff->passthrough.cred);
	if (is_sync_kiocb(iocb_fuse)) {
		file_start_write(passthrough_filp);
		ret = vfs_iter_write(
			passthrough_filp, iter, &iocb_fuse->ki_pos,
			fuse_iocb_to_rw_flags(iocb_fuse->ki_flags,
					      PASSTHROUGH_IOCB_MASK));
		file_end_write(passthrough_filp);
		if (ret > 0)
			fuse_copyattr(fuse_filp, passthrough_filp);
	} else {
		struct fuse_aio_req *aio_req;

		aio_req = kmalloc(sizeof(struct fuse_aio_req), GFP_KERNEL);
		if (!aio_req) {
			ret = -ENOMEM;
			goto out;
		}

		file_start_write(passthrough_filp);
		__sb_writers_release(passthrough_inode->i_sb, SB_FREEZE_WRITE);

		aio_req->iocb_fuse = iocb_fuse;
		kiocb_clone(&aio_req->iocb, iocb_fuse, passthrough_filp);
		aio_req->iocb.ki_complete = fuse_aio_rw_complete;
		ret = call_write_iter(passthrough_filp, &aio_req->iocb, iter);
		if (ret != -EIOCBQUEUED)
			fuse_aio_cleanup_handler(aio_req);
	}
out:
	revert_creds(old_cred);
	inode_unlock(fuse_inode);

	return ret;
}

ssize_t fuse_passthrough_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	const struct cred *old_cred;
	struct fuse_file *ff = file->private_data;
	struct file *passthrough_filp = ff->passthrough.filp;

	if (!passthrough_filp->f_op->mmap)
		return -ENODEV;

	if (WARN_ON(file != vma->vm_file))
		return -EIO;

	vma->vm_file = get_file(passthrough_filp);

	old_cred = override_creds(ff->passthrough.cred);
	ret = call_mmap(vma->vm_file, vma);
	revert_creds(old_cred);

	if (ret)
		fput(passthrough_filp);
	else
		fput(file);

	fuse_file_accessed(file, passthrough_filp);

	return ret;
}

int fuse_passthrough_open(struct fuse_dev *fud, u32 lower_fd)
{
	int res;
	struct file *passthrough_filp;
	struct fuse_conn *fc = fud->fc;
	struct inode *passthrough_inode;
	struct super_block *passthrough_sb;
	struct fuse_passthrough *passthrough;

	if (!fc->passthrough)
		return -EPERM;

	passthrough_filp = fget(lower_fd);
	if (!passthrough_filp) {
		pr_err("FUSE: invalid file descriptor for passthrough.\n");
		return -EBADF;
	}

	if (!passthrough_filp->f_op->read_iter ||
	    !passthrough_filp->f_op->write_iter) {
		pr_err("FUSE: passthrough file misses file operations.\n");
		res = -EBADF;
		goto err_free_file;
	}

	passthrough_inode = file_inode(passthrough_filp);
	passthrough_sb = passthrough_inode->i_sb;
	if (passthrough_sb->s_stack_depth >= FILESYSTEM_MAX_STACK_DEPTH) {
		pr_err("FUSE: fs stacking depth exceeded for passthrough\n");
		res = -EINVAL;
		goto err_free_file;
	}

	passthrough = kmalloc(sizeof(struct fuse_passthrough), GFP_KERNEL);
	if (!passthrough) {
		res = -ENOMEM;
		goto err_free_file;
	}

	passthrough->filp = passthrough_filp;
	passthrough->cred = prepare_creds();

	idr_preload(GFP_KERNEL);
	spin_lock(&fc->passthrough_req_lock);
	res = idr_alloc(&fc->passthrough_req, passthrough, 1, 0, GFP_ATOMIC);
	spin_unlock(&fc->passthrough_req_lock);
	idr_preload_end();

	if (res > 0)
		return res;

	fuse_passthrough_release(passthrough);
	kfree(passthrough);

err_free_file:
	fput(passthrough_filp);

	return res;
}

int fuse_passthrough_setup(struct fuse_conn *fc, struct fuse_file *ff,
			   struct fuse_open_out *openarg)
{
	struct fuse_passthrough *passthrough;
	int passthrough_fh = openarg->passthrough_fh;

	if (!fc->passthrough)
		return -EPERM;

	/* Default case, passthrough is not requested */
	if (passthrough_fh <= 0)
		return -EINVAL;

	spin_lock(&fc->passthrough_req_lock);
	passthrough = idr_remove(&fc->passthrough_req, passthrough_fh);
	spin_unlock(&fc->passthrough_req_lock);

	if (!passthrough)
		return -EINVAL;

	ff->passthrough = *passthrough;
	kfree(passthrough);

	return 0;
}

void fuse_passthrough_release(struct fuse_passthrough *passthrough)
{
	if (passthrough->filp) {
		fput(passthrough->filp);
		passthrough->filp = NULL;
	}
	if (passthrough->cred) {
		put_cred(passthrough->cred);
		passthrough->cred = NULL;
	}
=======
/*
 * FUSE passthrough to backing file.
 *
 * Copyright (c) 2023 CTERA Networks.
 */

#include "fuse_i.h"

#include <linux/file.h>
#include <linux/backing-file.h>
#include <linux/splice.h>

static void fuse_file_accessed(struct file *file)
{
	struct inode *inode = file_inode(file);

	fuse_invalidate_atime(inode);
}

static void fuse_file_modified(struct file *file)
{
	struct inode *inode = file_inode(file);

	fuse_invalidate_attr_mask(inode, FUSE_STATX_MODSIZE);
}

ssize_t fuse_passthrough_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	size_t count = iov_iter_count(iter);
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.user_file = file,
		.accessed = fuse_file_accessed,
	};


	pr_debug("%s: backing_file=0x%p, pos=%lld, len=%zu\n", __func__,
		 backing_file, iocb->ki_pos, count);

	if (!count)
		return 0;

	ret = backing_file_read_iter(backing_file, iter, iocb, iocb->ki_flags,
				     &ctx);

	return ret;
}

ssize_t fuse_passthrough_write_iter(struct kiocb *iocb,
				    struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	size_t count = iov_iter_count(iter);
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.user_file = file,
		.end_write = fuse_file_modified,
	};

	pr_debug("%s: backing_file=0x%p, pos=%lld, len=%zu\n", __func__,
		 backing_file, iocb->ki_pos, count);

	if (!count)
		return 0;

	inode_lock(inode);
	ret = backing_file_write_iter(backing_file, iter, iocb, iocb->ki_flags,
				      &ctx);
	inode_unlock(inode);

	return ret;
}

ssize_t fuse_passthrough_splice_read(struct file *in, loff_t *ppos,
				     struct pipe_inode_info *pipe,
				     size_t len, unsigned int flags)
{
	struct fuse_file *ff = in->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.user_file = in,
		.accessed = fuse_file_accessed,
	};

	pr_debug("%s: backing_file=0x%p, pos=%lld, len=%zu, flags=0x%x\n", __func__,
		 backing_file, ppos ? *ppos : 0, len, flags);

	return backing_file_splice_read(backing_file, ppos, pipe, len, flags,
					&ctx);
}

ssize_t fuse_passthrough_splice_write(struct pipe_inode_info *pipe,
				      struct file *out, loff_t *ppos,
				      size_t len, unsigned int flags)
{
	struct fuse_file *ff = out->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	struct inode *inode = file_inode(out);
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.user_file = out,
		.end_write = fuse_file_modified,
	};

	pr_debug("%s: backing_file=0x%p, pos=%lld, len=%zu, flags=0x%x\n", __func__,
		 backing_file, ppos ? *ppos : 0, len, flags);

	inode_lock(inode);
	ret = backing_file_splice_write(pipe, backing_file, ppos, len, flags,
					&ctx);
	inode_unlock(inode);

	return ret;
}

ssize_t fuse_passthrough_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = fuse_file_passthrough(ff);
	struct backing_file_ctx ctx = {
		.cred = ff->cred,
		.user_file = file,
		.accessed = fuse_file_accessed,
	};

	pr_debug("%s: backing_file=0x%p, start=%lu, end=%lu\n", __func__,
		 backing_file, vma->vm_start, vma->vm_end);

	return backing_file_mmap(backing_file, vma, &ctx);
}

struct fuse_backing *fuse_backing_get(struct fuse_backing *fb)
{
	if (fb && refcount_inc_not_zero(&fb->count))
		return fb;
	return NULL;
}

static void fuse_backing_free(struct fuse_backing *fb)
{
	pr_debug("%s: fb=0x%p\n", __func__, fb);

	if (fb->file)
		fput(fb->file);
	put_cred(fb->cred);
	kfree_rcu(fb, rcu);
}

void fuse_backing_put(struct fuse_backing *fb)
{
	if (fb && refcount_dec_and_test(&fb->count))
		fuse_backing_free(fb);
}

void fuse_backing_files_init(struct fuse_conn *fc)
{
	idr_init(&fc->backing_files_map);
}

static int fuse_backing_id_alloc(struct fuse_conn *fc, struct fuse_backing *fb)
{
	int id;

	idr_preload(GFP_KERNEL);
	spin_lock(&fc->lock);
	/* FIXME: xarray might be space inefficient */
	id = idr_alloc_cyclic(&fc->backing_files_map, fb, 1, 0, GFP_ATOMIC);
	spin_unlock(&fc->lock);
	idr_preload_end();

	WARN_ON_ONCE(id == 0);
	return id;
}

static struct fuse_backing *fuse_backing_id_remove(struct fuse_conn *fc,
						   int id)
{
	struct fuse_backing *fb;

	spin_lock(&fc->lock);
	fb = idr_remove(&fc->backing_files_map, id);
	spin_unlock(&fc->lock);

	return fb;
}

static int fuse_backing_id_free(int id, void *p, void *data)
{
	struct fuse_backing *fb = p;

	WARN_ON_ONCE(refcount_read(&fb->count) != 1);
	fuse_backing_free(fb);
	return 0;
}

void fuse_backing_files_free(struct fuse_conn *fc)
{
	idr_for_each(&fc->backing_files_map, fuse_backing_id_free, NULL);
	idr_destroy(&fc->backing_files_map);
}

int fuse_backing_open(struct fuse_conn *fc, struct fuse_backing_map *map)
{
	struct file *file;
	struct super_block *backing_sb;
	struct fuse_backing *fb = NULL;
	int res;

	pr_debug("%s: fd=%d flags=0x%x\n", __func__, map->fd, map->flags);

	/* TODO: relax CAP_SYS_ADMIN once backing files are visible to lsof */
	res = -EPERM;
	if (!fc->passthrough || !capable(CAP_SYS_ADMIN))
		goto out;

	res = -EINVAL;
	if (map->flags)
		goto out;

	file = fget(map->fd);
	res = -EBADF;
	if (!file)
		goto out;

	res = -EOPNOTSUPP;
	if (!file->f_op->read_iter || !file->f_op->write_iter)
		goto out_fput;

	backing_sb = file_inode(file)->i_sb;
	res = -ELOOP;
	if (backing_sb->s_stack_depth >= fc->max_stack_depth)
		goto out_fput;

	fb = kmalloc(sizeof(struct fuse_backing), GFP_KERNEL);
	res = -ENOMEM;
	if (!fb)
		goto out_fput;

	fb->file = file;
	fb->cred = prepare_creds();
	refcount_set(&fb->count, 1);

	res = fuse_backing_id_alloc(fc, fb);
	if (res < 0) {
		fuse_backing_free(fb);
		fb = NULL;
	}

out:
	pr_debug("%s: fb=0x%p, ret=%i\n", __func__, fb, res);

	return res;

out_fput:
	fput(file);
	goto out;
}

int fuse_backing_close(struct fuse_conn *fc, int backing_id)
{
	struct fuse_backing *fb = NULL;
	int err;

	pr_debug("%s: backing_id=%d\n", __func__, backing_id);

	/* TODO: relax CAP_SYS_ADMIN once backing files are visible to lsof */
	err = -EPERM;
	if (!fc->passthrough || !capable(CAP_SYS_ADMIN))
		goto out;

	err = -EINVAL;
	if (backing_id <= 0)
		goto out;

	err = -ENOENT;
	fb = fuse_backing_id_remove(fc, backing_id);
	if (!fb)
		goto out;

	fuse_backing_put(fb);
	err = 0;
out:
	pr_debug("%s: fb=0x%p, err=%i\n", __func__, fb, err);

	return err;
}

/*
 * Setup passthrough to a backing file.
 *
 * Returns an fb object with elevated refcount to be stored in fuse inode.
 */
struct fuse_backing *fuse_passthrough_open(struct file *file,
					   struct inode *inode,
					   int backing_id)
{
	struct fuse_file *ff = file->private_data;
	struct fuse_conn *fc = ff->fm->fc;
	struct fuse_backing *fb = NULL;
	struct file *backing_file;
	int err;

	err = -EINVAL;
	if (backing_id <= 0)
		goto out;

	rcu_read_lock();
	fb = idr_find(&fc->backing_files_map, backing_id);
	fb = fuse_backing_get(fb);
	rcu_read_unlock();

	err = -ENOENT;
	if (!fb)
		goto out;

	/* Allocate backing file per fuse file to store fuse path */
	backing_file = backing_file_open(&file->f_path, file->f_flags,
					 &fb->file->f_path, fb->cred);
	err = PTR_ERR(backing_file);
	if (IS_ERR(backing_file)) {
		fuse_backing_put(fb);
		goto out;
	}

	err = 0;
	ff->passthrough = backing_file;
	ff->cred = get_cred(fb->cred);
out:
	pr_debug("%s: backing_id=%d, fb=0x%p, backing_file=0x%p, err=%i\n", __func__,
		 backing_id, fb, ff->passthrough, err);

	return err ? ERR_PTR(err) : fb;
}

void fuse_passthrough_release(struct fuse_file *ff, struct fuse_backing *fb)
{
	pr_debug("%s: fb=0x%p, backing_file=0x%p\n", __func__,
		 fb, ff->passthrough);

	fput(ff->passthrough);
	ff->passthrough = NULL;
	put_cred(ff->cred);
	ff->cred = NULL;
>>>>>>> BRANCH (5e74df Merge tag 'x86-urgent-2024-03-24' of git://git.kernel.org/pu)
}
