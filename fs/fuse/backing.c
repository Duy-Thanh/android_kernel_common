// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE passthrough to backing file.
 *
 * Copyright (c) 2023 CTERA Networks.
 */

#include "fuse_i.h"

#include <linux/file.h>

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
<<<<<<< HEAD   (67cebb1706c5f2fb699a283dc634746d87108ab4 ANDROID: GKI: Update Honor abi symbol list)
	struct file *file;
	struct super_block *backing_sb;
	struct fuse_backing *fb = NULL;
||||||| BASE   (5467e3ebd4b972b412462ccdb26eb15c2424997c ANDROID: sched/deadline: Fix pick_next_pushable_dl_task)
	return NULL;
}

int fuse_lseek_initialize(struct fuse_bpf_args *fa, struct fuse_lseek_io *flio,
			  struct file *file, loff_t offset, int whence)
{
	struct fuse_file *fuse_file = file->private_data;

	flio->fli = (struct fuse_lseek_in) {
		.fh = fuse_file->fh,
		.offset = offset,
		.whence = whence,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(file->f_inode),
		.opcode = FUSE_LSEEK,
		.in_numargs = 1,
		.in_args[0].size = sizeof(flio->fli),
		.in_args[0].value = &flio->fli,
		.out_numargs = 1,
		.out_args[0].size = sizeof(flio->flo),
		.out_args[0].value = &flio->flo,
	};

	return 0;
}

int fuse_lseek_backing(struct fuse_bpf_args *fa, struct file *file, loff_t offset, int whence)
{
	const struct fuse_lseek_in *fli = fa->in_args[0].value;
	struct fuse_lseek_out *flo = fa->out_args[0].value;
	struct fuse_file *fuse_file = file->private_data;
	struct file *backing_file = fuse_file->backing_file;
	loff_t ret;

	if (offset == 0) {
		if (whence == SEEK_CUR) {
			flo->offset = file->f_pos;
			return 0;
		}

		if (whence == SEEK_SET) {
			flo->offset = vfs_setpos(file, 0, 0);
			return 0;
		}
	}

	inode_lock(file->f_inode);
	backing_file->f_pos = file->f_pos;
	ret = vfs_llseek(backing_file, fli->offset, fli->whence);

	if (!IS_ERR(ERR_PTR(ret))) {
		flo->offset = ret;
		ret = 0;
	}
	inode_unlock(file->f_inode);
	return ret;
}

void *fuse_lseek_finalize(struct fuse_bpf_args *fa, struct file *file, loff_t offset, int whence)
{
	struct fuse_lseek_out *flo = fa->out_args[0].value;

	if (!fa->error_in)
		file->f_pos = flo->offset;
	return ERR_PTR(flo->offset);
}

int fuse_copy_file_range_initialize(struct fuse_bpf_args *fa, struct fuse_copy_file_range_io *fcf,
				   struct file *file_in, loff_t pos_in, struct file *file_out,
				   loff_t pos_out, size_t len, unsigned int flags)
{
	struct fuse_file *fuse_file_in = file_in->private_data;
	struct fuse_file *fuse_file_out = file_out->private_data;


	fcf->fci = (struct fuse_copy_file_range_in) {
		.fh_in = fuse_file_in->fh,
		.off_in = pos_in,
		.nodeid_out = fuse_file_out->nodeid,
		.fh_out = fuse_file_out->fh,
		.off_out = pos_out,
		.len = len,
		.flags = flags,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(file_in->f_inode),
		.opcode = FUSE_COPY_FILE_RANGE,
		.in_numargs = 1,
		.in_args[0].size = sizeof(fcf->fci),
		.in_args[0].value = &fcf->fci,
		.out_numargs = 1,
		.out_args[0].size = sizeof(fcf->fwo),
		.out_args[0].value = &fcf->fwo,
	};

	return 0;
}

int fuse_copy_file_range_backing(struct fuse_bpf_args *fa, struct file *file_in, loff_t pos_in,
				 struct file *file_out, loff_t pos_out, size_t len,
				 unsigned int flags)
{
	const struct fuse_copy_file_range_in *fci = fa->in_args[0].value;
	struct fuse_file *fuse_file_in = file_in->private_data;
	struct file *backing_file_in = fuse_file_in->backing_file;
	struct fuse_file *fuse_file_out = file_out->private_data;
	struct file *backing_file_out = fuse_file_out->backing_file;

	/* TODO: Handle changing of in/out files */
	if (backing_file_out)
		return vfs_copy_file_range(backing_file_in, fci->off_in, backing_file_out,
					   fci->off_out, fci->len, fci->flags);
	return -EXDEV;
}

void *fuse_copy_file_range_finalize(struct fuse_bpf_args *fa, struct file *file_in, loff_t pos_in,
				    struct file *file_out, loff_t pos_out, size_t len,
				    unsigned int flags)
{
	return NULL;
}

int fuse_fsync_initialize(struct fuse_bpf_args *fa, struct fuse_fsync_in *ffi,
		   struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fuse_file *fuse_file = file->private_data;

	*ffi = (struct fuse_fsync_in) {
		.fh = fuse_file->fh,
		.fsync_flags = datasync ? FUSE_FSYNC_FDATASYNC : 0,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(file->f_inode)->nodeid,
		.opcode = FUSE_FSYNC,
		.in_numargs = 1,
		.in_args[0].size = sizeof(*ffi),
		.in_args[0].value = ffi,
		.flags = FUSE_BPF_FORCE,
	};

	return 0;
}

int fuse_fsync_backing(struct fuse_bpf_args *fa,
		   struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fuse_file *fuse_file = file->private_data;
	struct file *backing_file = fuse_file->backing_file;
	const struct fuse_fsync_in *ffi = fa->in_args[0].value;
	int new_datasync = (ffi->fsync_flags & FUSE_FSYNC_FDATASYNC) ? 1 : 0;

	return vfs_fsync(backing_file, new_datasync);
}

void *fuse_fsync_finalize(struct fuse_bpf_args *fa,
		   struct file *file, loff_t start, loff_t end, int datasync)
{
	return NULL;
}

int fuse_dir_fsync_initialize(struct fuse_bpf_args *fa, struct fuse_fsync_in *ffi,
		   struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fuse_file *fuse_file = file->private_data;

	*ffi = (struct fuse_fsync_in) {
		.fh = fuse_file->fh,
		.fsync_flags = datasync ? FUSE_FSYNC_FDATASYNC : 0,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(file->f_inode)->nodeid,
		.opcode = FUSE_FSYNCDIR,
		.in_numargs = 1,
		.in_args[0].size = sizeof(*ffi),
		.in_args[0].value = ffi,
		.flags = FUSE_BPF_FORCE,
	};

	return 0;
}

int fuse_getxattr_initialize(struct fuse_bpf_args *fa,
		struct fuse_getxattr_io *fgio,
		struct dentry *dentry, const char *name, void *value,
		size_t size)
{
	*fgio = (struct fuse_getxattr_io) {
		.fgi.size = size,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dentry->d_inode)->nodeid,
		.opcode = FUSE_GETXATTR,
		.in_numargs = 2,
		.out_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(fgio->fgi),
			.value = &fgio->fgi,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = strlen(name) + 1,
			.value = name,
		},
		.flags = size ? FUSE_BPF_OUT_ARGVAR : 0,
		.out_args[0].size = size ? size : sizeof(fgio->fgo),
		.out_args[0].value = size ? value : &fgio->fgo,
	};
	return 0;
}

int fuse_getxattr_backing(struct fuse_bpf_args *fa,
		struct dentry *dentry, const char *name, void *value,
		size_t size)
{
	ssize_t ret = vfs_getxattr(&nop_mnt_idmap,
		get_fuse_dentry(dentry)->backing_path.dentry,
		fa->in_args[1].value, value, size);

	if (fa->flags & FUSE_BPF_OUT_ARGVAR)
		fa->out_args[0].size = ret;
	else
		((struct fuse_getxattr_out *)fa->out_args[0].value)->size = ret;

	return 0;
}

void *fuse_getxattr_finalize(struct fuse_bpf_args *fa,
		struct dentry *dentry, const char *name, void *value,
		size_t size)
{
	struct fuse_getxattr_out *fgo;

	if (fa->flags & FUSE_BPF_OUT_ARGVAR)
		return ERR_PTR(fa->out_args[0].size);

	fgo = fa->out_args[0].value;

	return ERR_PTR(fgo->size);

}

int fuse_listxattr_initialize(struct fuse_bpf_args *fa,
			      struct fuse_getxattr_io *fgio,
			      struct dentry *dentry, char *list, size_t size)
{
	*fgio = (struct fuse_getxattr_io){
		.fgi.size = size,
	};

	*fa = (struct fuse_bpf_args){
		.nodeid = get_fuse_inode(dentry->d_inode)->nodeid,
		.opcode = FUSE_LISTXATTR,
		.in_numargs = 1,
		.out_numargs = 1,
		.in_args[0] =
			(struct fuse_bpf_in_arg){
				.size = sizeof(fgio->fgi),
				.value = &fgio->fgi,
			},
		.flags = size ? FUSE_BPF_OUT_ARGVAR : 0,
		.out_args[0].size = size ? size : sizeof(fgio->fgo),
		.out_args[0].value = size ? (void *)list : &fgio->fgo,
	};

	return 0;
}

int fuse_listxattr_backing(struct fuse_bpf_args *fa, struct dentry *dentry,
			   char *list, size_t size)
{
	ssize_t ret =
		vfs_listxattr(get_fuse_dentry(dentry)->backing_path.dentry,
			      list, size);

	if (ret < 0)
		return ret;

	if (fa->flags & FUSE_BPF_OUT_ARGVAR)
		fa->out_args[0].size = ret;
	else
		((struct fuse_getxattr_out *)fa->out_args[0].value)->size = ret;

	return ret;
}

void *fuse_listxattr_finalize(struct fuse_bpf_args *fa, struct dentry *dentry,
			      char *list, size_t size)
{
	struct fuse_getxattr_out *fgo;

	if (fa->error_in)
		return NULL;

	if (fa->flags & FUSE_BPF_OUT_ARGVAR)
		return ERR_PTR(fa->out_args[0].size);

	fgo = fa->out_args[0].value;
	return ERR_PTR(fgo->size);
}

int fuse_setxattr_initialize(struct fuse_bpf_args *fa,
			     struct fuse_setxattr_in *fsxi,
			     struct dentry *dentry, const char *name,
			     const void *value, size_t size, int flags)
{
	*fsxi = (struct fuse_setxattr_in) {
		.size = size,
		.flags = flags,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dentry->d_inode)->nodeid,
		.opcode = FUSE_SETXATTR,
		.in_numargs = 3,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fsxi),
			.value = fsxi,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = strlen(name) + 1,
			.value = name,
		},
		.in_args[2] = (struct fuse_bpf_in_arg) {
			.size = size,
			.value = value,
		},
	};

	return 0;
}

int fuse_setxattr_backing(struct fuse_bpf_args *fa, struct dentry *dentry,
			  const char *name, const void *value, size_t size,
			  int flags)
{
	return vfs_setxattr(&nop_mnt_idmap,
			    get_fuse_dentry(dentry)->backing_path.dentry, name,
			    value, size, flags);
}

void *fuse_setxattr_finalize(struct fuse_bpf_args *fa, struct dentry *dentry,
			     const char *name, const void *value, size_t size,
			     int flags)
{
	return NULL;
}

int fuse_removexattr_initialize(struct fuse_bpf_args *fa,
				struct fuse_dummy_io *unused,
				struct dentry *dentry, const char *name)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dentry->d_inode)->nodeid,
		.opcode = FUSE_REMOVEXATTR,
		.in_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = strlen(name) + 1,
			.value = name,
		},
	};

	return 0;
}

int fuse_removexattr_backing(struct fuse_bpf_args *fa,
			     struct dentry *dentry, const char *name)
{
	struct path *backing_path =
		&get_fuse_dentry(dentry)->backing_path;

	/* TODO account for changes of the name by prefilter */
	return vfs_removexattr(&nop_mnt_idmap, backing_path->dentry, name);
}

void *fuse_removexattr_finalize(struct fuse_bpf_args *fa,
				struct dentry *dentry, const char *name)
{
	return NULL;
}

static inline void fuse_bpf_aio_put(struct fuse_bpf_aio_req *aio_req)
{
	if (refcount_dec_and_test(&aio_req->ref))
		kmem_cache_free(fuse_bpf_aio_request_cachep, aio_req);
}

static void fuse_bpf_aio_cleanup_handler(struct fuse_bpf_aio_req *aio_req)
{
	struct kiocb *iocb = &aio_req->iocb;
	struct kiocb *iocb_orig = aio_req->iocb_orig;

	if (iocb->ki_flags & IOCB_WRITE) {
		kiocb_end_write(iocb);
		fuse_copyattr(iocb_orig->ki_filp, iocb->ki_filp);
	}
	iocb_orig->ki_pos = iocb->ki_pos;
	fuse_bpf_aio_put(aio_req);
}

static void fuse_bpf_aio_rw_complete(struct kiocb *iocb, long res)
{
	struct fuse_bpf_aio_req *aio_req =
		container_of(iocb, struct fuse_bpf_aio_req, iocb);
	struct kiocb *iocb_orig = aio_req->iocb_orig;

	fuse_bpf_aio_cleanup_handler(aio_req);
	iocb_orig->ki_complete(iocb_orig, res);
}


int fuse_file_read_iter_initialize(
		struct fuse_bpf_args *fa, struct fuse_file_read_iter_io *fri,
		struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;

	fri->fri = (struct fuse_read_in) {
		.fh = ff->fh,
		.offset = iocb->ki_pos,
		.size = to->count,
	};

	/* TODO we can't assume 'to' is a kvec */
	/* TODO we also can't assume the vector has only one component */
	*fa = (struct fuse_bpf_args) {
		.opcode = FUSE_READ,
		.nodeid = ff->nodeid,
		.in_numargs = 1,
		.in_args[0].size = sizeof(fri->fri),
		.in_args[0].value = &fri->fri,
		.out_numargs = 1,
		.out_args[0].size = sizeof(fri->frio),
		.out_args[0].value = &fri->frio,
		/*
		 * TODO Design this properly.
		 * Possible approach: do not pass buf to bpf
		 * If going to userland, do a deep copy
		 * For extra credit, do that to/from the vector, rather than
		 * making an extra copy in the kernel
		 */
	};

	return 0;
}

int fuse_file_read_iter_backing(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *to)
{
	struct fuse_read_iter_out *frio = fa->out_args[0].value;
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	ssize_t ret;

	if (!iov_iter_count(to))
		return 0;

	/* TODO This just plain ignores any change to fuse_read_in */
	if (is_sync_kiocb(iocb)) {
		ret = vfs_iter_read(ff->backing_file, to, &iocb->ki_pos,
				iocb->ki_flags & FUSE_BPF_IOCB_MASK);
	} else {
		struct fuse_bpf_aio_req *aio_req;

		ret = -ENOMEM;
		aio_req = kmem_cache_zalloc(fuse_bpf_aio_request_cachep, GFP_KERNEL);
		if (!aio_req)
			goto out;

		aio_req->iocb_orig = iocb;
		kiocb_clone(&aio_req->iocb, iocb, ff->backing_file);
		aio_req->iocb.ki_complete = fuse_bpf_aio_rw_complete;
		refcount_set(&aio_req->ref, 2);
		ret = vfs_iocb_iter_read(ff->backing_file, &aio_req->iocb, to);
		fuse_bpf_aio_put(aio_req);
		if (ret != -EIOCBQUEUED)
			fuse_bpf_aio_cleanup_handler(aio_req);
	}

	/* TODO Need to point value at the buffer for post-modification */

out:
	fuse_file_accessed(file, ff->backing_file);

	frio->ret = ret;
	return ret < 0 ? ret : 0;
}

void *fuse_file_read_iter_finalize(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *to)
{
	struct fuse_read_iter_out *frio = fa->out_args[0].value;

	return ERR_PTR(frio->ret);
}

int fuse_file_write_iter_initialize(
		struct fuse_bpf_args *fa, struct fuse_file_write_iter_io *fwio,
		struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;

	*fwio = (struct fuse_file_write_iter_io) {
		.fwi.fh = ff->fh,
		.fwi.offset = iocb->ki_pos,
		.fwi.size = from->count,
	};

	/* TODO we can't assume 'from' is a kvec */
	*fa = (struct fuse_bpf_args) {
		.opcode = FUSE_WRITE,
		.nodeid = ff->nodeid,
		.in_numargs = 2,
		.in_args[0].size = sizeof(fwio->fwi),
		.in_args[0].value = &fwio->fwi,
		.in_args[1].size = fwio->fwi.size,
		.in_args[1].value = iov_iter_is_kvec(from)
			? from->kvec->iov_base : NULL,
		.out_numargs = 1,
		.out_args[0].size = sizeof(fwio->fwio),
		.out_args[0].value = &fwio->fwio,
	};

	return 0;
}

int fuse_file_write_iter_backing(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct fuse_write_iter_out *fwio = fa->out_args[0].value;
	ssize_t ret;

	if (!iov_iter_count(from))
		return 0;

	/* TODO This just plain ignores any change to fuse_write_in */
	/* TODO uint32_t seems smaller than ssize_t.... right? */
	inode_lock(file_inode(file));

	fuse_copyattr(file, ff->backing_file);

	if (is_sync_kiocb(iocb)) {
		ret = vfs_iter_write(ff->backing_file, from, &iocb->ki_pos,
					   iocb->ki_flags & FUSE_BPF_IOCB_MASK);

		/* Must reflect change in size of backing file to upper file */
		if (ret > 0)
			fuse_copyattr(file, ff->backing_file);
	} else {
		struct fuse_bpf_aio_req *aio_req;

		ret = -ENOMEM;
		aio_req = kmem_cache_zalloc(fuse_bpf_aio_request_cachep, GFP_KERNEL);
		if (!aio_req)
			goto out;

		aio_req->iocb_orig = iocb;
		kiocb_clone(&aio_req->iocb, iocb, ff->backing_file);
		aio_req->iocb.ki_complete = fuse_bpf_aio_rw_complete;
		refcount_set(&aio_req->ref, 2);
		ret = vfs_iocb_iter_write(ff->backing_file, &aio_req->iocb, from);
		fuse_bpf_aio_put(aio_req);
		if (ret != -EIOCBQUEUED)
			fuse_bpf_aio_cleanup_handler(aio_req);
	}

out:
	inode_unlock(file_inode(file));
	fwio->ret = ret;
	if (ret < 0)
		return ret;
	return 0;
}

void *fuse_file_write_iter_finalize(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *from)
{
	struct fuse_write_iter_out *fwio = fa->out_args[0].value;

	return ERR_PTR(fwio->ret);
}

ssize_t fuse_splice_read_backing(struct file *in, loff_t *ppos,
		struct pipe_inode_info *pipe, size_t len, unsigned long flags)
{
	struct fuse_file *ff = in->private_data;
	ssize_t ret;

	ret = vfs_splice_read(ff->backing_file, ppos, pipe, len, flags);
	fuse_file_accessed(in, ff->backing_file);

	return ret;
}

ssize_t fuse_splice_write_backing(struct pipe_inode_info *pipe,
		struct file *out, loff_t *ppos, size_t len, unsigned long flags)
{
	ssize_t ret;
	struct fuse_file *ff = out->private_data;

	inode_lock(file_inode(out));
	file_start_write(ff->backing_file);
	ret = iter_file_splice_write(pipe, ff->backing_file, ppos, len, flags);
	file_end_write(ff->backing_file);
	if (ret > 0)
		fuse_copyattr(out, ff->backing_file);
	inode_unlock(file_inode(out));
	return ret;
}

long fuse_backing_ioctl(struct file *file, unsigned int command, unsigned long arg, int flags)
{
	struct fuse_file *ff = file->private_data;
	long ret;

	if (flags & FUSE_IOCTL_COMPAT)
		ret = -ENOTTY;
	else
		ret = vfs_ioctl(ff->backing_file, command, arg);

	return ret;
}

int fuse_file_flock_backing(struct file *file, int cmd, struct file_lock *fl)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = ff->backing_file;
	int error;

	fl->c.flc_file = backing_file;
	if (backing_file->f_op->flock)
		error = backing_file->f_op->flock(backing_file, cmd, fl);
	else
		error = locks_lock_file_wait(backing_file, fl);
	return error;
}

ssize_t fuse_backing_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	struct fuse_file *ff = file->private_data;
	struct inode *fuse_inode = file_inode(file);
	struct file *backing_file = ff->backing_file;
	struct inode *backing_inode = file_inode(backing_file);
	struct timespec64 fuse_inode_ctime, backing_inode_ctime;
	struct timespec64 fuse_inode_mtime, backing_inode_mtime;

	if (!backing_file->f_op->mmap)
		return -ENODEV;

	if (WARN_ON(file != vma->vm_file))
		return -EIO;

	vma->vm_file = get_file(backing_file);

	ret = call_mmap(vma->vm_file, vma);

	if (ret)
		fput(backing_file);
	else
		fput(file);

	if (file->f_flags & O_NOATIME)
		return ret;

	fuse_inode_ctime = inode_get_ctime(fuse_inode);
	backing_inode_ctime = inode_get_ctime(backing_inode);
	fuse_inode_mtime = inode_get_mtime(fuse_inode);
	backing_inode_mtime = inode_get_mtime(backing_inode);
	if ((!timespec64_equal(&fuse_inode_mtime, &backing_inode_mtime) ||
	     !timespec64_equal(&fuse_inode_ctime, &backing_inode_ctime))) {
		inode_set_mtime_to_ts(fuse_inode, backing_inode_mtime);
		inode_set_ctime_to_ts(fuse_inode, backing_inode_ctime);
	}
	touch_atime(&file->f_path);

	return ret;
}

int fuse_file_fallocate_initialize(struct fuse_bpf_args *fa,
		struct fuse_fallocate_in *ffi,
		struct file *file, int mode, loff_t offset, loff_t length)
{
	struct fuse_file *ff = file->private_data;

	*ffi = (struct fuse_fallocate_in) {
		.fh = ff->fh,
		.offset = offset,
		.length = length,
		.mode = mode
	};

	*fa = (struct fuse_bpf_args) {
		.opcode = FUSE_FALLOCATE,
		.nodeid = ff->nodeid,
		.in_numargs = 1,
		.in_args[0].size = sizeof(*ffi),
		.in_args[0].value = ffi,
	};

	return 0;
}

int fuse_file_fallocate_backing(struct fuse_bpf_args *fa,
		struct file *file, int mode, loff_t offset, loff_t length)
{
	const struct fuse_fallocate_in *ffi = fa->in_args[0].value;
	struct fuse_file *ff = file->private_data;

	return vfs_fallocate(ff->backing_file, ffi->mode, ffi->offset,
			     ffi->length);
}

void *fuse_file_fallocate_finalize(struct fuse_bpf_args *fa,
		struct file *file, int mode, loff_t offset, loff_t length)
{
	return NULL;
}

/*******************************************************************************
 * Directory operations after here                                             *
 ******************************************************************************/

int fuse_lookup_initialize(struct fuse_bpf_args *fa, struct fuse_lookup_io *fli,
	       struct inode *dir, struct dentry *entry, unsigned int flags)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dir)->nodeid,
		.opcode = FUSE_LOOKUP,
		.in_numargs = 1,
		.out_numargs = 2,
		.flags = FUSE_BPF_OUT_ARGVAR,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
		.out_args[0] = (struct fuse_bpf_arg) {
			.size = sizeof(fli->feo),
			.value = &fli->feo,
		},
		.out_args[1] = (struct fuse_bpf_arg) {
			.size = sizeof(fli->feb.out),
			.value = &fli->feb.out,
		},
	};

	return 0;
}

int fuse_lookup_backing(struct fuse_bpf_args *fa, struct inode *dir,
			  struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_entry = get_fuse_dentry(entry);
	struct fuse_dentry *dir_fuse_entry = get_fuse_dentry(entry->d_parent);
	struct dentry *dir_backing_entry = dir_fuse_entry->backing_path.dentry;
	struct inode *dir_backing_inode = dir_backing_entry->d_inode;
	struct dentry *backing_entry;
	struct fuse_entry_out *feo = (void *)fa->out_args[0].value;
	struct kstat stat;
	int err;

	inode_lock_nested(dir_backing_inode, I_MUTEX_PARENT);
	backing_entry = lookup_one_len(entry->d_name.name, dir_backing_entry,
					strlen(entry->d_name.name));
	inode_unlock(dir_backing_inode);

	if (IS_ERR(backing_entry))
		return PTR_ERR(backing_entry);

	fuse_entry->backing_path = (struct path) {
		.dentry = backing_entry,
		.mnt = mntget(dir_fuse_entry->backing_path.mnt),
	};

	if (d_is_negative(backing_entry)) {
		fa->error_in = -ENOENT;
		return 0;
	}

	err = follow_down(&fuse_entry->backing_path, 0);
	if (err)
		goto err_out;

	err = vfs_getattr(&fuse_entry->backing_path, &stat,
				  STATX_BASIC_STATS, 0);
	if (err)
		goto err_out;

	fuse_stat_to_attr(get_fuse_conn(dir),
			  backing_entry->d_inode, &stat, &feo->attr);
	return 0;

err_out:
	path_put(&fuse_entry->backing_path);
	fuse_entry->backing_path = (struct path) { };
	return err;
}

int fuse_handle_backing(struct fuse_entry_bpf *feb, struct inode **backing_inode,
			struct path *backing_path)
{
	switch (feb->out.backing_action) {
	case FUSE_ACTION_KEEP:
		/* backing inode/path are added in fuse_lookup_backing */
		break;

	case FUSE_ACTION_REMOVE:
		iput(*backing_inode);
		*backing_inode = NULL;
		path_put(backing_path);
		*backing_path = (struct path) { };
		break;

	case FUSE_ACTION_REPLACE: {
		struct file *backing_file = feb->backing_file;

		if (!backing_file)
			return -EINVAL;
		if (IS_ERR(backing_file))
			return PTR_ERR(backing_file);

		if (backing_inode)
			iput(*backing_inode);
		*backing_inode = backing_file->f_inode;
		ihold(*backing_inode);

		path_put(backing_path);
		*backing_path = backing_file->f_path;
		path_get(backing_path);
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

int fuse_handle_bpf_prog(struct fuse_entry_bpf *feb, struct inode *parent,
			 struct bpf_prog **bpf)
{
	struct bpf_prog *new_bpf = NULL;

	switch (feb->out.bpf_action) {
	case FUSE_ACTION_KEEP: {
		/* Parent isn't presented, but we want to keep
		 * Don't touch bpf program at all in this case
		 */
		if (!parent)
			return 0;

		new_bpf = get_fuse_inode(parent)->bpf;
		if (new_bpf)
			bpf_prog_inc(new_bpf);
		break;
	}

	case FUSE_ACTION_REMOVE:
		break;

	case FUSE_ACTION_REPLACE: {
		struct file *bpf_file = feb->bpf_file;

		if (!bpf_file)
			return -EINVAL;
		if (IS_ERR(bpf_file))
			return PTR_ERR(bpf_file);

		new_bpf = fuse_get_bpf_prog(bpf_file);
		if (IS_ERR(new_bpf))
			return PTR_ERR(new_bpf);
		break;
	}

	default:
		return -EINVAL;
	}

	/* Cannot change existing program */
	if (*bpf) {
		if (new_bpf)
			bpf_prog_put(new_bpf);
		return new_bpf == *bpf ? 0 : -EINVAL;
	}

	*bpf = new_bpf;
	return 0;
}

struct dentry *fuse_lookup_finalize(struct fuse_bpf_args *fa, struct inode *dir,
			   struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_entry;
	struct dentry *backing_entry;
	struct inode *inode = NULL, *backing_inode;
	struct inode *entry_inode = entry->d_inode;
	struct fuse_entry_out *feo = fa->out_args[0].value;
	struct fuse_entry_bpf_out *febo = fa->out_args[1].value;
	struct fuse_entry_bpf *feb = container_of(febo, struct fuse_entry_bpf,
						  out);
	int error = -1;
	u64 target_nodeid = 0;
	struct dentry *ret = NULL;

	fuse_entry = get_fuse_dentry(entry);
	if (!fuse_entry) {
		ret = ERR_PTR(-EIO);
		goto out;
	}

	backing_entry = fuse_entry->backing_path.dentry;
	if (!backing_entry) {
		ret = ERR_PTR(-ENOENT);
		goto out;
	}

	if (entry_inode)
		target_nodeid = get_fuse_inode(entry_inode)->nodeid;

	backing_inode = backing_entry->d_inode;
	if (backing_inode)
		inode = fuse_iget_backing(dir->i_sb, target_nodeid,
					  backing_inode);

	error = inode ?
		fuse_handle_bpf_prog(feb, dir, &get_fuse_inode(inode)->bpf) :
		fuse_handle_bpf_prog(feb, dir, &fuse_entry->bpf);
	if (error) {
		ret = ERR_PTR(error);
		goto out;
	}

	if (inode) {
		error = fuse_handle_backing(feb,
					&get_fuse_inode(inode)->backing_inode,
					&fuse_entry->backing_path);
		if (error) {
			ret = ERR_PTR(error);
			goto out;
		}

		get_fuse_inode(inode)->nodeid = feo->nodeid;
		ret = d_splice_alias(inode, entry);
		if (!IS_ERR(ret))
			inode = NULL;
	}
out:
	iput(inode);
	if (feb->backing_file)
		fput(feb->backing_file);
	return ret;
}

int fuse_revalidate_backing(struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_dentry = get_fuse_dentry(entry);
	struct dentry *backing_entry = fuse_dentry->backing_path.dentry;

	spin_lock(&backing_entry->d_lock);
	if (d_unhashed(backing_entry)) {
		spin_unlock(&backing_entry->d_lock);
			return 0;
	}
	spin_unlock(&backing_entry->d_lock);

	if (unlikely(backing_entry->d_flags & DCACHE_OP_REVALIDATE))
		return backing_entry->d_op->d_revalidate(backing_entry, flags);
	return 1;
}

int fuse_canonical_path_initialize(struct fuse_bpf_args *fa,
				   struct fuse_dummy_io *fdi,
				   const struct path *path,
				   struct path *canonical_path)
{
	fa->opcode = FUSE_CANONICAL_PATH;
	return 0;
}

int fuse_canonical_path_backing(struct fuse_bpf_args *fa, const struct path *path,
				struct path *canonical_path)
{
	get_fuse_backing_path(path->dentry, canonical_path);
	return 0;
}

void *fuse_canonical_path_finalize(struct fuse_bpf_args *fa,
				   const struct path *path,
				   struct path *canonical_path)
{
	return NULL;
}

int fuse_mknod_initialize(
		struct fuse_bpf_args *fa, struct fuse_mknod_in *fmi,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev)
{
	*fmi = (struct fuse_mknod_in) {
		.mode = mode,
		.rdev = new_encode_dev(rdev),
		.umask = current_umask(),
	};
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(dir),
		.opcode = FUSE_MKNOD,
		.in_numargs = 2,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fmi),
			.value = fmi,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
	};

	return 0;
}

int fuse_mknod_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev)
{
	int err = 0;
	const struct fuse_mknod_in *fmi = fa->in_args[0].value;
	struct fuse_inode *fuse_inode = get_fuse_inode(dir);
	struct inode *backing_inode = fuse_inode->backing_inode;
	struct path backing_path = {};
	struct inode *inode = NULL;

	//TODO Actually deal with changing the backing entry in mknod
	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	mode = fmi->mode;
	if (!IS_POSIXACL(backing_inode))
		mode &= ~fmi->umask;
	err = vfs_mknod(&nop_mnt_idmap, backing_inode, backing_path.dentry, mode,
	                new_decode_dev(fmi->rdev));
	inode_unlock(backing_inode);
	if (err)
		goto out;
	if (d_really_is_negative(backing_path.dentry) ||
		unlikely(d_unhashed(backing_path.dentry))) {
		err = -EINVAL;
		/**
		 * TODO: overlayfs responds to this situation with a
		 * lookupOneLen. Should we do that too?
		 */
		goto out;
	}
	inode = fuse_iget_backing(dir->i_sb, fuse_inode->nodeid, backing_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	d_instantiate(entry, inode);
out:
	path_put(&backing_path);
	return err;
}

void *fuse_mknod_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev)
{
	return NULL;
}

int fuse_mkdir_initialize(
		struct fuse_bpf_args *fa, struct fuse_mkdir_in *fmi,
		struct inode *dir, struct dentry *entry, umode_t mode)
{
	*fmi = (struct fuse_mkdir_in) {
		.mode = mode,
		.umask = current_umask(),
	};
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(dir),
		.opcode = FUSE_MKDIR,
		.in_numargs = 2,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fmi),
			.value = fmi,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
	};

	return 0;
}

int fuse_mkdir_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir_inode, struct dentry *entry, umode_t mode)
{
	int err = 0;
	const struct fuse_mkdir_in *fmi = fa->in_args[0].value;
	struct fuse_inode *dir_fuse_inode = get_fuse_inode(dir_inode);
	struct inode *dir_backing_inode = dir_fuse_inode->backing_inode;
	struct path backing_path = {};
	struct inode *inode = NULL;

	//TODO Actually deal with changing the backing entry in mkdir
	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	inode_lock_nested(dir_backing_inode, I_MUTEX_PARENT);
	mode = fmi->mode;
	if (!IS_POSIXACL(dir_backing_inode))
		mode &= ~fmi->umask;
	err = vfs_mkdir(&nop_mnt_idmap, dir_backing_inode, backing_path.dentry, mode);
	if (err)
		goto out;
	if (d_really_is_negative(backing_path.dentry) ||
		unlikely(d_unhashed(backing_path.dentry))) {
		struct dentry *d = lookup_one_len(entry->d_name.name,
					backing_path.dentry->d_parent,
					entry->d_name.len);

		if (IS_ERR(d)) {
			err = PTR_ERR(d);
			goto out;
		}
		dput(backing_path.dentry);
		backing_path.dentry = d;
	}
	inode = fuse_iget_backing(dir_inode->i_sb, 0,
				  backing_path.dentry->d_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	d_instantiate(entry, inode);
	if (get_fuse_inode(inode)->bpf)
		bpf_prog_put(get_fuse_inode(inode)->bpf);
	get_fuse_inode(inode)->bpf = get_fuse_dentry(entry)->bpf;
	get_fuse_dentry(entry)->bpf = NULL;
out:
	inode_unlock(dir_backing_inode);
	path_put(&backing_path);
	return err;
}

void *fuse_mkdir_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode)
{
	return NULL;
}

int fuse_rmdir_initialize(
		struct fuse_bpf_args *fa, struct fuse_dummy_io *dummy,
		struct inode *dir, struct dentry *entry)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(dir),
		.opcode = FUSE_RMDIR,
		.in_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
	};

	return 0;
}

int fuse_rmdir_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry)
{
	int err = 0;
	struct path backing_path = {};
	struct dentry *backing_parent_dentry;
	struct inode *backing_inode;

	/* TODO Actually deal with changing the backing entry in rmdir */
	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	/* TODO Not sure if we should reverify like overlayfs, or get inode from d_parent */
	backing_parent_dentry = dget_parent(backing_path.dentry);
	backing_inode = d_inode(backing_parent_dentry);

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	err = vfs_rmdir(&nop_mnt_idmap, backing_inode, backing_path.dentry);
	inode_unlock(backing_inode);

	dput(backing_parent_dentry);
	if (!err)
		d_drop(entry);
	path_put(&backing_path);
	return err;
}

void *fuse_rmdir_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry)
{
	return NULL;
}

static int fuse_rename_backing_common(
			 struct inode *olddir, struct dentry *oldent,
			 struct inode *newdir, struct dentry *newent,
			 unsigned int flags)
{
	int err = 0;
	struct path old_backing_path;
	struct path new_backing_path;
	struct dentry *old_backing_dir_dentry;
	struct dentry *old_backing_dentry;
	struct dentry *new_backing_dir_dentry;
	struct dentry *new_backing_dentry;
	struct dentry *trap = NULL;
	struct inode *target_inode;
	struct renamedata rd;

	//TODO Actually deal with changing anything that isn't a flag
	get_fuse_backing_path(oldent, &old_backing_path);
	if (!old_backing_path.dentry)
		return -EBADF;
	get_fuse_backing_path(newent, &new_backing_path);
	if (!new_backing_path.dentry) {
		/*
		 * TODO A file being moved from a backing path to another
		 * backing path which is not yet instrumented with FUSE-BPF.
		 * This may be slow and should be substituted with something
		 * more clever.
		 */
		err = -EXDEV;
		goto put_old_path;
	}
	if (new_backing_path.mnt != old_backing_path.mnt) {
		err = -EXDEV;
		goto put_new_path;
	}
	old_backing_dentry = old_backing_path.dentry;
	new_backing_dentry = new_backing_path.dentry;
	old_backing_dir_dentry = dget_parent(old_backing_dentry);
	new_backing_dir_dentry = dget_parent(new_backing_dentry);
	target_inode = d_inode(newent);

	trap = lock_rename(old_backing_dir_dentry, new_backing_dir_dentry);
	if (trap == old_backing_dentry) {
		err = -EINVAL;
		goto put_parents;
	}
	if (trap == new_backing_dentry) {
		err = -ENOTEMPTY;
		goto put_parents;
	}
	rd = (struct renamedata) {
		.old_mnt_idmap = &nop_mnt_idmap,
		.old_dir = d_inode(old_backing_dir_dentry),
		.old_dentry = old_backing_dentry,
		.new_mnt_idmap = &nop_mnt_idmap,
		.new_dir = d_inode(new_backing_dir_dentry),
		.new_dentry = new_backing_dentry,
		.flags = flags,
	};
	err = vfs_rename(&rd);
	if (err)
		goto unlock;
	if (target_inode)
		fsstack_copy_attr_all(target_inode,
				get_fuse_inode(target_inode)->backing_inode);
	fsstack_copy_attr_all(d_inode(oldent), d_inode(old_backing_dentry));
unlock:
	unlock_rename(old_backing_dir_dentry, new_backing_dir_dentry);
put_parents:
	dput(new_backing_dir_dentry);
	dput(old_backing_dir_dentry);
put_new_path:
	path_put(&new_backing_path);
put_old_path:
	path_put(&old_backing_path);
	return err;
}

int fuse_rename2_initialize(struct fuse_bpf_args *fa, struct fuse_rename2_in *fri,
			    struct inode *olddir, struct dentry *oldent,
			    struct inode *newdir, struct dentry *newent,
			    unsigned int flags)
{
	*fri = (struct fuse_rename2_in) {
		.newdir = get_node_id(newdir),
		.flags = flags,
	};
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(olddir),
		.opcode = FUSE_RENAME2,
		.in_numargs = 3,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fri),
			.value = fri,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = oldent->d_name.len + 1,
			.value = oldent->d_name.name,
		},
		.in_args[2] = (struct fuse_bpf_in_arg) {
			.size = newent->d_name.len + 1,
			.value = newent->d_name.name,
		},
	};

	return 0;
}

int fuse_rename2_backing(struct fuse_bpf_args *fa,
			 struct inode *olddir, struct dentry *oldent,
			 struct inode *newdir, struct dentry *newent,
			 unsigned int flags)
{
	const struct fuse_rename2_in *fri = fa->in_args[0].value;

	/* TODO: deal with changing dirs/ents */
	return fuse_rename_backing_common(olddir, oldent, newdir, newent, fri->flags);
}

void *fuse_rename2_finalize(struct fuse_bpf_args *fa,
			    struct inode *olddir, struct dentry *oldent,
			    struct inode *newdir, struct dentry *newent,
			    unsigned int flags)
{
	return NULL;
}

int fuse_rename_initialize(struct fuse_bpf_args *fa, struct fuse_rename_in *fri,
			   struct inode *olddir, struct dentry *oldent,
			   struct inode *newdir, struct dentry *newent)
{
	*fri = (struct fuse_rename_in) {
		.newdir = get_node_id(newdir),
	};
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(olddir),
		.opcode = FUSE_RENAME,
		.in_numargs = 3,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fri),
			.value = fri,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = oldent->d_name.len + 1,
			.value = oldent->d_name.name,
		},
		.in_args[2] = (struct fuse_bpf_in_arg) {
			.size = newent->d_name.len + 1,
			.value = newent->d_name.name,
		},
	};

	return 0;
}

int fuse_rename_backing(struct fuse_bpf_args *fa,
			struct inode *olddir, struct dentry *oldent,
			struct inode *newdir, struct dentry *newent)
{
	/* TODO: deal with changing dirs/ents */
	return fuse_rename_backing_common(olddir, oldent, newdir, newent, 0);
}

void *fuse_rename_finalize(struct fuse_bpf_args *fa,
			   struct inode *olddir, struct dentry *oldent,
			   struct inode *newdir, struct dentry *newent)
{
	return NULL;
}

int fuse_unlink_initialize(
		struct fuse_bpf_args *fa, struct fuse_dummy_io *dummy,
		struct inode *dir, struct dentry *entry)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(dir),
		.opcode = FUSE_UNLINK,
		.in_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
	};

	return 0;
}

int fuse_unlink_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry)
{
	int err = 0;
	struct path backing_path = {};
	struct dentry *backing_parent_dentry;
	struct inode *backing_inode;

	/* TODO Actually deal with changing the backing entry in unlink */
	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	/* TODO Not sure if we should reverify like overlayfs, or get inode from d_parent */
	backing_parent_dentry = dget_parent(backing_path.dentry);
	backing_inode = d_inode(backing_parent_dentry);

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	err = vfs_unlink(&nop_mnt_idmap, backing_inode, backing_path.dentry, NULL);
	inode_unlock(backing_inode);

	dput(backing_parent_dentry);
	if (!err)
		d_drop(entry);
	path_put(&backing_path);
	return err;
}

void *fuse_unlink_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry)
{
	return NULL;
}

int fuse_link_initialize(struct fuse_bpf_args *fa, struct fuse_link_in *fli,
			 struct dentry *entry, struct inode *dir,
			 struct dentry *newent)
{
	struct inode *src_inode = entry->d_inode;

	*fli = (struct fuse_link_in){
		.oldnodeid = get_node_id(src_inode),
	};

	fa->opcode = FUSE_LINK;
	fa->in_numargs = 2;
	fa->in_args[0].size = sizeof(*fli);
	fa->in_args[0].value = fli;
	fa->in_args[1].size = newent->d_name.len + 1;
	fa->in_args[1].value = newent->d_name.name;

	return 0;
}

int fuse_link_backing(struct fuse_bpf_args *fa, struct dentry *entry,
		      struct inode *dir, struct dentry *newent)
{
	int err = 0;
	struct path backing_old_path = {};
	struct path backing_new_path = {};
	struct dentry *backing_dir_dentry;
	struct inode *fuse_new_inode = NULL;
	struct fuse_inode *fuse_dir_inode = get_fuse_inode(dir);
	struct inode *backing_dir_inode = fuse_dir_inode->backing_inode;

	get_fuse_backing_path(entry, &backing_old_path);
	if (!backing_old_path.dentry)
		return -EBADF;

	get_fuse_backing_path(newent, &backing_new_path);
	if (!backing_new_path.dentry) {
		err = -EBADF;
		goto err_dst_path;
	}

	backing_dir_dentry = dget_parent(backing_new_path.dentry);
	backing_dir_inode = d_inode(backing_dir_dentry);

	inode_lock_nested(backing_dir_inode, I_MUTEX_PARENT);
	err = vfs_link(backing_old_path.dentry, &nop_mnt_idmap,
		       backing_dir_inode, backing_new_path.dentry, NULL);
	inode_unlock(backing_dir_inode);
	if (err)
		goto out;

	if (d_really_is_negative(backing_new_path.dentry) ||
	    unlikely(d_unhashed(backing_new_path.dentry))) {
		err = -EINVAL;
		/**
		 * TODO: overlayfs responds to this situation with a
		 * lookupOneLen. Should we do that too?
		 */
		goto out;
	}

	fuse_new_inode = fuse_iget_backing(dir->i_sb, fuse_dir_inode->nodeid, backing_dir_inode);
	if (IS_ERR(fuse_new_inode)) {
		err = PTR_ERR(fuse_new_inode);
		goto out;
	}
	d_instantiate(newent, fuse_new_inode);

out:
	dput(backing_dir_dentry);
	path_put(&backing_new_path);
err_dst_path:
	path_put(&backing_old_path);
	return err;
}

void *fuse_link_finalize(struct fuse_bpf_args *fa, struct dentry *entry,
			 struct inode *dir, struct dentry *newent)
{
	return NULL;
}

int fuse_getattr_initialize(struct fuse_bpf_args *fa, struct fuse_getattr_io *fgio,
			const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	fgio->fgi = (struct fuse_getattr_in) {
		.getattr_flags = flags,
		.fh = -1, /* TODO is this OK? */
	};

	fgio->fao = (struct fuse_attr_out) {0};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(entry->d_inode),
		.opcode = FUSE_GETATTR,
		.in_numargs = 1,
		.out_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(fgio->fgi),
			.value = &fgio->fgi,
		},
		.out_args[0] = (struct fuse_bpf_arg) {
			.size = sizeof(fgio->fao),
			.value = &fgio->fao,
		},
	};

	return 0;
}

static void fuse_stat_to_attr(struct fuse_conn *fc, struct inode *inode,
		struct kstat *stat, struct fuse_attr *attr)
{
	unsigned int blkbits;

	/* see the comment in fuse_change_attributes() */
	if (fc->writeback_cache && S_ISREG(inode->i_mode)) {
		stat->size = i_size_read(inode);
		stat->mtime.tv_sec = inode_get_mtime_sec(inode);;
		stat->mtime.tv_nsec = inode_get_mtime_nsec(inode);
		stat->ctime.tv_sec = inode_get_ctime_sec(inode);
		stat->ctime.tv_nsec = inode_get_ctime_nsec(inode);
	}

	attr->ino = stat->ino;
	attr->mode = (inode->i_mode & S_IFMT) | (stat->mode & 07777);
	attr->nlink = stat->nlink;
	attr->uid = from_kuid(fc->user_ns, stat->uid);
	attr->gid = from_kgid(fc->user_ns, stat->gid);
	attr->atime = stat->atime.tv_sec;
	attr->atimensec = stat->atime.tv_nsec;
	attr->mtime = stat->mtime.tv_sec;
	attr->mtimensec = stat->mtime.tv_nsec;
	attr->ctime = stat->ctime.tv_sec;
	attr->ctimensec = stat->ctime.tv_nsec;
	attr->size = stat->size;
	attr->blocks = stat->blocks;

	if (stat->blksize != 0)
		blkbits = ilog2(stat->blksize);
	else
		blkbits = inode->i_sb->s_blocksize_bits;

	attr->blksize = 1 << blkbits;
}

int fuse_getattr_backing(struct fuse_bpf_args *fa,
		const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	struct path *backing_path =
		&get_fuse_dentry(entry)->backing_path;
	struct inode *backing_inode = backing_path->dentry->d_inode;
	struct fuse_attr_out *fao = fa->out_args[0].value;
	struct kstat tmp;
	int err;

	if (!stat)
		stat = &tmp;

	if (flags & AT_GETATTR_NOSEC)
		err = vfs_getattr_nosec(backing_path, stat, request_mask, flags);
	else
		err = vfs_getattr(backing_path, stat, request_mask, flags);

	if (!err)
		fuse_stat_to_attr(get_fuse_conn(entry->d_inode),
				  backing_inode, stat, &fao->attr);

	return err;
}

void *fuse_getattr_finalize(struct fuse_bpf_args *fa,
			const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	struct fuse_attr_out *outarg = fa->out_args[0].value;
	struct inode *inode = entry->d_inode;
	u64 attr_version = fuse_get_attr_version(get_fuse_mount(inode)->fc);
	int err = 0;

	/* TODO: Ensure this doesn't happen if we had an error getting attrs in
	 * backing.
	 */
	err = finalize_attr(inode, outarg, attr_version, stat);
	return ERR_PTR(err);
}

static void fattr_to_iattr(struct fuse_conn *fc,
			   const struct fuse_setattr_in *arg,
			   struct iattr *iattr)
{
	unsigned int fvalid = arg->valid;

	if (fvalid & FATTR_MODE)
		iattr->ia_valid |= ATTR_MODE, iattr->ia_mode = arg->mode;
	if (fvalid & FATTR_UID) {
		iattr->ia_valid |= ATTR_UID;
		iattr->ia_uid = make_kuid(fc->user_ns, arg->uid);
	}
	if (fvalid & FATTR_GID) {
		iattr->ia_valid |= ATTR_GID;
		iattr->ia_gid = make_kgid(fc->user_ns, arg->gid);
	}
	if (fvalid & FATTR_SIZE)
		iattr->ia_valid |= ATTR_SIZE,  iattr->ia_size = arg->size;
	if (fvalid & FATTR_ATIME) {
		iattr->ia_valid |= ATTR_ATIME;
		iattr->ia_atime.tv_sec = arg->atime;
		iattr->ia_atime.tv_nsec = arg->atimensec;
		if (!(fvalid & FATTR_ATIME_NOW))
			iattr->ia_valid |= ATTR_ATIME_SET;
	}
	if (fvalid & FATTR_MTIME) {
		iattr->ia_valid |= ATTR_MTIME;
		iattr->ia_mtime.tv_sec = arg->mtime;
		iattr->ia_mtime.tv_nsec = arg->mtimensec;
		if (!(fvalid & FATTR_MTIME_NOW))
			iattr->ia_valid |= ATTR_MTIME_SET;
	}
	if (fvalid & FATTR_CTIME) {
		iattr->ia_valid |= ATTR_CTIME;
		iattr->ia_ctime.tv_sec = arg->ctime;
		iattr->ia_ctime.tv_nsec = arg->ctimensec;
	}
}

int fuse_setattr_initialize(struct fuse_bpf_args *fa, struct fuse_setattr_io *fsio,
		struct dentry *dentry, struct iattr *attr, struct file *file)
{
	struct fuse_conn *fc = get_fuse_conn(dentry->d_inode);

	*fsio = (struct fuse_setattr_io) {0};
	iattr_to_fattr(&nop_mnt_idmap, fc, attr, &fsio->fsi, true);

	*fa = (struct fuse_bpf_args) {
		.opcode = FUSE_SETATTR,
		.nodeid = get_node_id(dentry->d_inode),
		.in_numargs = 1,
		.in_args[0].size = sizeof(fsio->fsi),
		.in_args[0].value = &fsio->fsi,
		.out_numargs = 1,
		.out_args[0].size = sizeof(fsio->fao),
		.out_args[0].value = &fsio->fao,
	};

	return 0;
}

int fuse_setattr_backing(struct fuse_bpf_args *fa,
		struct dentry *dentry, struct iattr *attr, struct file *file)
{
	struct fuse_conn *fc = get_fuse_conn(dentry->d_inode);
	const struct fuse_setattr_in *fsi = fa->in_args[0].value;
	struct iattr new_attr = {0};
	struct path *backing_path = &get_fuse_dentry(dentry)->backing_path;
=======
	return NULL;
}

int fuse_lseek_initialize(struct fuse_bpf_args *fa, struct fuse_lseek_io *flio,
			  struct file *file, loff_t offset, int whence)
{
	struct fuse_file *fuse_file = file->private_data;

	flio->fli = (struct fuse_lseek_in) {
		.fh = fuse_file->fh,
		.offset = offset,
		.whence = whence,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(file->f_inode),
		.opcode = FUSE_LSEEK,
		.in_numargs = 1,
		.in_args[0].size = sizeof(flio->fli),
		.in_args[0].value = &flio->fli,
		.out_numargs = 1,
		.out_args[0].size = sizeof(flio->flo),
		.out_args[0].value = &flio->flo,
	};

	return 0;
}

int fuse_lseek_backing(struct fuse_bpf_args *fa, struct file *file, loff_t offset, int whence)
{
	const struct fuse_lseek_in *fli = fa->in_args[0].value;
	struct fuse_lseek_out *flo = fa->out_args[0].value;
	struct fuse_file *fuse_file = file->private_data;
	struct file *backing_file = fuse_file->backing_file;
	loff_t ret;

	if (offset == 0) {
		if (whence == SEEK_CUR) {
			flo->offset = file->f_pos;
			return 0;
		}

		if (whence == SEEK_SET) {
			flo->offset = vfs_setpos(file, 0, 0);
			return 0;
		}
	}

	inode_lock(file->f_inode);
	backing_file->f_pos = file->f_pos;
	ret = vfs_llseek(backing_file, fli->offset, fli->whence);

	if (!IS_ERR(ERR_PTR(ret))) {
		flo->offset = ret;
		ret = 0;
	}
	inode_unlock(file->f_inode);
	return ret;
}

void *fuse_lseek_finalize(struct fuse_bpf_args *fa, struct file *file, loff_t offset, int whence)
{
	struct fuse_lseek_out *flo = fa->out_args[0].value;

	if (!fa->error_in)
		file->f_pos = flo->offset;
	return ERR_PTR(flo->offset);
}

int fuse_copy_file_range_initialize(struct fuse_bpf_args *fa, struct fuse_copy_file_range_io *fcf,
				   struct file *file_in, loff_t pos_in, struct file *file_out,
				   loff_t pos_out, size_t len, unsigned int flags)
{
	struct fuse_file *fuse_file_in = file_in->private_data;
	struct fuse_file *fuse_file_out = file_out->private_data;


	fcf->fci = (struct fuse_copy_file_range_in) {
		.fh_in = fuse_file_in->fh,
		.off_in = pos_in,
		.nodeid_out = fuse_file_out->nodeid,
		.fh_out = fuse_file_out->fh,
		.off_out = pos_out,
		.len = len,
		.flags = flags,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(file_in->f_inode),
		.opcode = FUSE_COPY_FILE_RANGE,
		.in_numargs = 1,
		.in_args[0].size = sizeof(fcf->fci),
		.in_args[0].value = &fcf->fci,
		.out_numargs = 1,
		.out_args[0].size = sizeof(fcf->fwo),
		.out_args[0].value = &fcf->fwo,
	};

	return 0;
}

int fuse_copy_file_range_backing(struct fuse_bpf_args *fa, struct file *file_in, loff_t pos_in,
				 struct file *file_out, loff_t pos_out, size_t len,
				 unsigned int flags)
{
	const struct fuse_copy_file_range_in *fci = fa->in_args[0].value;
	struct fuse_file *fuse_file_in = file_in->private_data;
	struct file *backing_file_in = fuse_file_in->backing_file;
	struct fuse_file *fuse_file_out = file_out->private_data;
	struct file *backing_file_out = fuse_file_out->backing_file;

	/* TODO: Handle changing of in/out files */
	if (backing_file_out)
		return vfs_copy_file_range(backing_file_in, fci->off_in, backing_file_out,
					   fci->off_out, fci->len, fci->flags);
	return -EXDEV;
}

void *fuse_copy_file_range_finalize(struct fuse_bpf_args *fa, struct file *file_in, loff_t pos_in,
				    struct file *file_out, loff_t pos_out, size_t len,
				    unsigned int flags)
{
	return NULL;
}

int fuse_fsync_initialize(struct fuse_bpf_args *fa, struct fuse_fsync_in *ffi,
		   struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fuse_file *fuse_file = file->private_data;

	*ffi = (struct fuse_fsync_in) {
		.fh = fuse_file->fh,
		.fsync_flags = datasync ? FUSE_FSYNC_FDATASYNC : 0,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(file->f_inode)->nodeid,
		.opcode = FUSE_FSYNC,
		.in_numargs = 1,
		.in_args[0].size = sizeof(*ffi),
		.in_args[0].value = ffi,
		.flags = FUSE_BPF_FORCE,
	};

	return 0;
}

int fuse_fsync_backing(struct fuse_bpf_args *fa,
		   struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fuse_file *fuse_file = file->private_data;
	struct file *backing_file = fuse_file->backing_file;
	const struct fuse_fsync_in *ffi = fa->in_args[0].value;
	int new_datasync = (ffi->fsync_flags & FUSE_FSYNC_FDATASYNC) ? 1 : 0;

	return vfs_fsync(backing_file, new_datasync);
}

void *fuse_fsync_finalize(struct fuse_bpf_args *fa,
		   struct file *file, loff_t start, loff_t end, int datasync)
{
	return NULL;
}

int fuse_dir_fsync_initialize(struct fuse_bpf_args *fa, struct fuse_fsync_in *ffi,
		   struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fuse_file *fuse_file = file->private_data;

	*ffi = (struct fuse_fsync_in) {
		.fh = fuse_file->fh,
		.fsync_flags = datasync ? FUSE_FSYNC_FDATASYNC : 0,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(file->f_inode)->nodeid,
		.opcode = FUSE_FSYNCDIR,
		.in_numargs = 1,
		.in_args[0].size = sizeof(*ffi),
		.in_args[0].value = ffi,
		.flags = FUSE_BPF_FORCE,
	};

	return 0;
}

int fuse_getxattr_initialize(struct fuse_bpf_args *fa,
		struct fuse_getxattr_io *fgio,
		struct dentry *dentry, const char *name, void *value,
		size_t size)
{
	*fgio = (struct fuse_getxattr_io) {
		.fgi.size = size,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dentry->d_inode)->nodeid,
		.opcode = FUSE_GETXATTR,
		.in_numargs = 2,
		.out_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(fgio->fgi),
			.value = &fgio->fgi,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = strlen(name) + 1,
			.value = name,
		},
		.flags = size ? FUSE_BPF_OUT_ARGVAR : 0,
		.out_args[0].size = size ? size : sizeof(fgio->fgo),
		.out_args[0].value = size ? value : &fgio->fgo,
	};
	return 0;
}

int fuse_getxattr_backing(struct fuse_bpf_args *fa,
		struct dentry *dentry, const char *name, void *value,
		size_t size)
{
	ssize_t ret = vfs_getxattr(&nop_mnt_idmap,
		get_fuse_dentry(dentry)->backing_path.dentry,
		fa->in_args[1].value, value, size);

	if (fa->flags & FUSE_BPF_OUT_ARGVAR)
		fa->out_args[0].size = ret;
	else
		((struct fuse_getxattr_out *)fa->out_args[0].value)->size = ret;

	return 0;
}

void *fuse_getxattr_finalize(struct fuse_bpf_args *fa,
		struct dentry *dentry, const char *name, void *value,
		size_t size)
{
	struct fuse_getxattr_out *fgo;

	if (fa->flags & FUSE_BPF_OUT_ARGVAR)
		return ERR_PTR(fa->out_args[0].size);

	fgo = fa->out_args[0].value;

	return ERR_PTR(fgo->size);

}

int fuse_listxattr_initialize(struct fuse_bpf_args *fa,
			      struct fuse_getxattr_io *fgio,
			      struct dentry *dentry, char *list, size_t size)
{
	*fgio = (struct fuse_getxattr_io){
		.fgi.size = size,
	};

	*fa = (struct fuse_bpf_args){
		.nodeid = get_fuse_inode(dentry->d_inode)->nodeid,
		.opcode = FUSE_LISTXATTR,
		.in_numargs = 1,
		.out_numargs = 1,
		.in_args[0] =
			(struct fuse_bpf_in_arg){
				.size = sizeof(fgio->fgi),
				.value = &fgio->fgi,
			},
		.flags = size ? FUSE_BPF_OUT_ARGVAR : 0,
		.out_args[0].size = size ? size : sizeof(fgio->fgo),
		.out_args[0].value = size ? (void *)list : &fgio->fgo,
	};

	return 0;
}

int fuse_listxattr_backing(struct fuse_bpf_args *fa, struct dentry *dentry,
			   char *list, size_t size)
{
	ssize_t ret =
		vfs_listxattr(get_fuse_dentry(dentry)->backing_path.dentry,
			      list, size);

	if (ret < 0)
		return ret;

	if (fa->flags & FUSE_BPF_OUT_ARGVAR)
		fa->out_args[0].size = ret;
	else
		((struct fuse_getxattr_out *)fa->out_args[0].value)->size = ret;

	return ret;
}

void *fuse_listxattr_finalize(struct fuse_bpf_args *fa, struct dentry *dentry,
			      char *list, size_t size)
{
	struct fuse_getxattr_out *fgo;

	if (fa->error_in)
		return NULL;

	if (fa->flags & FUSE_BPF_OUT_ARGVAR)
		return ERR_PTR(fa->out_args[0].size);

	fgo = fa->out_args[0].value;
	return ERR_PTR(fgo->size);
}

int fuse_setxattr_initialize(struct fuse_bpf_args *fa,
			     struct fuse_setxattr_in *fsxi,
			     struct dentry *dentry, const char *name,
			     const void *value, size_t size, int flags)
{
	*fsxi = (struct fuse_setxattr_in) {
		.size = size,
		.flags = flags,
	};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dentry->d_inode)->nodeid,
		.opcode = FUSE_SETXATTR,
		.in_numargs = 3,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fsxi),
			.value = fsxi,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = strlen(name) + 1,
			.value = name,
		},
		.in_args[2] = (struct fuse_bpf_in_arg) {
			.size = size,
			.value = value,
		},
	};

	return 0;
}

int fuse_setxattr_backing(struct fuse_bpf_args *fa, struct dentry *dentry,
			  const char *name, const void *value, size_t size,
			  int flags)
{
	return vfs_setxattr(&nop_mnt_idmap,
			    get_fuse_dentry(dentry)->backing_path.dentry, name,
			    value, size, flags);
}

void *fuse_setxattr_finalize(struct fuse_bpf_args *fa, struct dentry *dentry,
			     const char *name, const void *value, size_t size,
			     int flags)
{
	return NULL;
}

int fuse_removexattr_initialize(struct fuse_bpf_args *fa,
				struct fuse_dummy_io *unused,
				struct dentry *dentry, const char *name)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dentry->d_inode)->nodeid,
		.opcode = FUSE_REMOVEXATTR,
		.in_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = strlen(name) + 1,
			.value = name,
		},
	};

	return 0;
}

int fuse_removexattr_backing(struct fuse_bpf_args *fa,
			     struct dentry *dentry, const char *name)
{
	struct path *backing_path =
		&get_fuse_dentry(dentry)->backing_path;

	/* TODO account for changes of the name by prefilter */
	return vfs_removexattr(&nop_mnt_idmap, backing_path->dentry, name);
}

void *fuse_removexattr_finalize(struct fuse_bpf_args *fa,
				struct dentry *dentry, const char *name)
{
	return NULL;
}

static inline void fuse_bpf_aio_put(struct fuse_bpf_aio_req *aio_req)
{
	if (refcount_dec_and_test(&aio_req->ref))
		kmem_cache_free(fuse_bpf_aio_request_cachep, aio_req);
}

static void fuse_bpf_aio_cleanup_handler(struct fuse_bpf_aio_req *aio_req)
{
	struct kiocb *iocb = &aio_req->iocb;
	struct kiocb *iocb_orig = aio_req->iocb_orig;

	if (iocb->ki_flags & IOCB_WRITE) {
		kiocb_end_write(iocb);
		fuse_copyattr(iocb_orig->ki_filp, iocb->ki_filp);
	}
	iocb_orig->ki_pos = iocb->ki_pos;
	fuse_bpf_aio_put(aio_req);
}

static void fuse_bpf_aio_rw_complete(struct kiocb *iocb, long res)
{
	struct fuse_bpf_aio_req *aio_req =
		container_of(iocb, struct fuse_bpf_aio_req, iocb);
	struct kiocb *iocb_orig = aio_req->iocb_orig;

	fuse_bpf_aio_cleanup_handler(aio_req);
	iocb_orig->ki_complete(iocb_orig, res);
}


int fuse_file_read_iter_initialize(
		struct fuse_bpf_args *fa, struct fuse_file_read_iter_io *fri,
		struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;

	fri->fri = (struct fuse_read_in) {
		.fh = ff->fh,
		.offset = iocb->ki_pos,
		.size = to->count,
	};

	/* TODO we can't assume 'to' is a kvec */
	/* TODO we also can't assume the vector has only one component */
	*fa = (struct fuse_bpf_args) {
		.opcode = FUSE_READ,
		.nodeid = ff->nodeid,
		.in_numargs = 1,
		.in_args[0].size = sizeof(fri->fri),
		.in_args[0].value = &fri->fri,
		.out_numargs = 1,
		.out_args[0].size = sizeof(fri->frio),
		.out_args[0].value = &fri->frio,
		/*
		 * TODO Design this properly.
		 * Possible approach: do not pass buf to bpf
		 * If going to userland, do a deep copy
		 * For extra credit, do that to/from the vector, rather than
		 * making an extra copy in the kernel
		 */
	};

	return 0;
}

int fuse_file_read_iter_backing(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *to)
{
	struct fuse_read_iter_out *frio = fa->out_args[0].value;
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	ssize_t ret;

	if (!iov_iter_count(to))
		return 0;

	/* TODO This just plain ignores any change to fuse_read_in */
	if (is_sync_kiocb(iocb)) {
		ret = vfs_iter_read(ff->backing_file, to, &iocb->ki_pos,
				iocb->ki_flags & FUSE_BPF_IOCB_MASK);
	} else {
		struct fuse_bpf_aio_req *aio_req;

		ret = -ENOMEM;
		aio_req = kmem_cache_zalloc(fuse_bpf_aio_request_cachep, GFP_KERNEL);
		if (!aio_req)
			goto out;

		aio_req->iocb_orig = iocb;
		kiocb_clone(&aio_req->iocb, iocb, ff->backing_file);
		aio_req->iocb.ki_complete = fuse_bpf_aio_rw_complete;
		refcount_set(&aio_req->ref, 2);
		ret = vfs_iocb_iter_read(ff->backing_file, &aio_req->iocb, to);
		fuse_bpf_aio_put(aio_req);
		if (ret != -EIOCBQUEUED)
			fuse_bpf_aio_cleanup_handler(aio_req);
	}

	/* TODO Need to point value at the buffer for post-modification */

out:
	fuse_file_accessed(file, ff->backing_file);

	frio->ret = ret;
	return ret < 0 ? ret : 0;
}

void *fuse_file_read_iter_finalize(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *to)
{
	struct fuse_read_iter_out *frio = fa->out_args[0].value;

	return ERR_PTR(frio->ret);
}

int fuse_file_write_iter_initialize(
		struct fuse_bpf_args *fa, struct fuse_file_write_iter_io *fwio,
		struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;

	*fwio = (struct fuse_file_write_iter_io) {
		.fwi.fh = ff->fh,
		.fwi.offset = iocb->ki_pos,
		.fwi.size = from->count,
	};

	/* TODO we can't assume 'from' is a kvec */
	*fa = (struct fuse_bpf_args) {
		.opcode = FUSE_WRITE,
		.nodeid = ff->nodeid,
		.in_numargs = 2,
		.in_args[0].size = sizeof(fwio->fwi),
		.in_args[0].value = &fwio->fwi,
		.in_args[1].size = fwio->fwi.size,
		.in_args[1].value = iov_iter_is_kvec(from)
			? from->kvec->iov_base : NULL,
		.out_numargs = 1,
		.out_args[0].size = sizeof(fwio->fwio),
		.out_args[0].value = &fwio->fwio,
	};

	return 0;
}

int fuse_file_write_iter_backing(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct fuse_file *ff = file->private_data;
	struct fuse_write_iter_out *fwio = fa->out_args[0].value;
	ssize_t ret;

	if (!iov_iter_count(from))
		return 0;

	/* TODO This just plain ignores any change to fuse_write_in */
	/* TODO uint32_t seems smaller than ssize_t.... right? */
	inode_lock(file_inode(file));

	fuse_copyattr(file, ff->backing_file);

	if (is_sync_kiocb(iocb)) {
		ret = vfs_iter_write(ff->backing_file, from, &iocb->ki_pos,
					   iocb->ki_flags & FUSE_BPF_IOCB_MASK);

		/* Must reflect change in size of backing file to upper file */
		if (ret > 0)
			fuse_copyattr(file, ff->backing_file);
	} else {
		struct fuse_bpf_aio_req *aio_req;

		ret = -ENOMEM;
		aio_req = kmem_cache_zalloc(fuse_bpf_aio_request_cachep, GFP_KERNEL);
		if (!aio_req)
			goto out;

		aio_req->iocb_orig = iocb;
		kiocb_clone(&aio_req->iocb, iocb, ff->backing_file);
		aio_req->iocb.ki_complete = fuse_bpf_aio_rw_complete;
		refcount_set(&aio_req->ref, 2);
		ret = vfs_iocb_iter_write(ff->backing_file, &aio_req->iocb, from);
		fuse_bpf_aio_put(aio_req);
		if (ret != -EIOCBQUEUED)
			fuse_bpf_aio_cleanup_handler(aio_req);
	}

out:
	inode_unlock(file_inode(file));
	fwio->ret = ret;
	if (ret < 0)
		return ret;
	return 0;
}

void *fuse_file_write_iter_finalize(struct fuse_bpf_args *fa,
		struct kiocb *iocb, struct iov_iter *from)
{
	struct fuse_write_iter_out *fwio = fa->out_args[0].value;

	return ERR_PTR(fwio->ret);
}

ssize_t fuse_splice_read_backing(struct file *in, loff_t *ppos,
		struct pipe_inode_info *pipe, size_t len, unsigned long flags)
{
	struct fuse_file *ff = in->private_data;
	ssize_t ret;

	ret = vfs_splice_read(ff->backing_file, ppos, pipe, len, flags);
	fuse_file_accessed(in, ff->backing_file);

	return ret;
}

ssize_t fuse_splice_write_backing(struct pipe_inode_info *pipe,
		struct file *out, loff_t *ppos, size_t len, unsigned long flags)
{
	ssize_t ret;
	struct fuse_file *ff = out->private_data;

	inode_lock(file_inode(out));
	file_start_write(ff->backing_file);
	ret = iter_file_splice_write(pipe, ff->backing_file, ppos, len, flags);
	file_end_write(ff->backing_file);
	if (ret > 0)
		fuse_copyattr(out, ff->backing_file);
	inode_unlock(file_inode(out));
	return ret;
}

long fuse_backing_ioctl(struct file *file, unsigned int command, unsigned long arg, int flags)
{
	struct fuse_file *ff = file->private_data;
	long ret;

	if (flags & FUSE_IOCTL_COMPAT)
		ret = -ENOTTY;
	else
		ret = vfs_ioctl(ff->backing_file, command, arg);

	return ret;
}

int fuse_file_flock_backing(struct file *file, int cmd, struct file_lock *fl)
{
	struct fuse_file *ff = file->private_data;
	struct file *backing_file = ff->backing_file;
	int error;

	fl->c.flc_file = backing_file;
	if (backing_file->f_op->flock)
		error = backing_file->f_op->flock(backing_file, cmd, fl);
	else
		error = locks_lock_file_wait(backing_file, fl);
	return error;
}

ssize_t fuse_backing_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret;
	struct fuse_file *ff = file->private_data;
	struct inode *fuse_inode = file_inode(file);
	struct file *backing_file = ff->backing_file;
	struct inode *backing_inode = file_inode(backing_file);
	struct timespec64 fuse_inode_ctime, backing_inode_ctime;
	struct timespec64 fuse_inode_mtime, backing_inode_mtime;

	if (!backing_file->f_op->mmap)
		return -ENODEV;

	if (WARN_ON(file != vma->vm_file))
		return -EIO;

	vma_set_file(vma, backing_file);
	ret = call_mmap(vma->vm_file, vma);
	if (ret)
		return ret;

	if (file->f_flags & O_NOATIME)
		return ret;

	fuse_inode_ctime = inode_get_ctime(fuse_inode);
	backing_inode_ctime = inode_get_ctime(backing_inode);
	fuse_inode_mtime = inode_get_mtime(fuse_inode);
	backing_inode_mtime = inode_get_mtime(backing_inode);
	if ((!timespec64_equal(&fuse_inode_mtime, &backing_inode_mtime) ||
	     !timespec64_equal(&fuse_inode_ctime, &backing_inode_ctime))) {
		inode_set_mtime_to_ts(fuse_inode, backing_inode_mtime);
		inode_set_ctime_to_ts(fuse_inode, backing_inode_ctime);
	}
	touch_atime(&file->f_path);

	return ret;
}

int fuse_file_fallocate_initialize(struct fuse_bpf_args *fa,
		struct fuse_fallocate_in *ffi,
		struct file *file, int mode, loff_t offset, loff_t length)
{
	struct fuse_file *ff = file->private_data;

	*ffi = (struct fuse_fallocate_in) {
		.fh = ff->fh,
		.offset = offset,
		.length = length,
		.mode = mode
	};

	*fa = (struct fuse_bpf_args) {
		.opcode = FUSE_FALLOCATE,
		.nodeid = ff->nodeid,
		.in_numargs = 1,
		.in_args[0].size = sizeof(*ffi),
		.in_args[0].value = ffi,
	};

	return 0;
}

int fuse_file_fallocate_backing(struct fuse_bpf_args *fa,
		struct file *file, int mode, loff_t offset, loff_t length)
{
	const struct fuse_fallocate_in *ffi = fa->in_args[0].value;
	struct fuse_file *ff = file->private_data;

	return vfs_fallocate(ff->backing_file, ffi->mode, ffi->offset,
			     ffi->length);
}

void *fuse_file_fallocate_finalize(struct fuse_bpf_args *fa,
		struct file *file, int mode, loff_t offset, loff_t length)
{
	return NULL;
}

/*******************************************************************************
 * Directory operations after here                                             *
 ******************************************************************************/

int fuse_lookup_initialize(struct fuse_bpf_args *fa, struct fuse_lookup_io *fli,
	       struct inode *dir, struct dentry *entry, unsigned int flags)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_fuse_inode(dir)->nodeid,
		.opcode = FUSE_LOOKUP,
		.in_numargs = 1,
		.out_numargs = 2,
		.flags = FUSE_BPF_OUT_ARGVAR,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
		.out_args[0] = (struct fuse_bpf_arg) {
			.size = sizeof(fli->feo),
			.value = &fli->feo,
		},
		.out_args[1] = (struct fuse_bpf_arg) {
			.size = sizeof(fli->feb.out),
			.value = &fli->feb.out,
		},
	};

	return 0;
}

int fuse_lookup_backing(struct fuse_bpf_args *fa, struct inode *dir,
			  struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_entry = get_fuse_dentry(entry);
	struct fuse_dentry *dir_fuse_entry = get_fuse_dentry(entry->d_parent);
	struct dentry *dir_backing_entry = dir_fuse_entry->backing_path.dentry;
	struct inode *dir_backing_inode = dir_backing_entry->d_inode;
	struct dentry *backing_entry;
	struct fuse_entry_out *feo = (void *)fa->out_args[0].value;
	struct kstat stat;
	int err;

	inode_lock_nested(dir_backing_inode, I_MUTEX_PARENT);
	backing_entry = lookup_one_len(entry->d_name.name, dir_backing_entry,
					strlen(entry->d_name.name));
	inode_unlock(dir_backing_inode);

	if (IS_ERR(backing_entry))
		return PTR_ERR(backing_entry);

	fuse_entry->backing_path = (struct path) {
		.dentry = backing_entry,
		.mnt = mntget(dir_fuse_entry->backing_path.mnt),
	};

	if (d_is_negative(backing_entry)) {
		fa->error_in = -ENOENT;
		return 0;
	}

	err = follow_down(&fuse_entry->backing_path, 0);
	if (err)
		goto err_out;

	err = vfs_getattr(&fuse_entry->backing_path, &stat,
				  STATX_BASIC_STATS, 0);
	if (err)
		goto err_out;

	fuse_stat_to_attr(get_fuse_conn(dir),
			  backing_entry->d_inode, &stat, &feo->attr);
	return 0;

err_out:
	path_put(&fuse_entry->backing_path);
	fuse_entry->backing_path = (struct path) { };
	return err;
}

int fuse_handle_backing(struct fuse_entry_bpf *feb, struct inode **backing_inode,
			struct path *backing_path)
{
	switch (feb->out.backing_action) {
	case FUSE_ACTION_KEEP:
		/* backing inode/path are added in fuse_lookup_backing */
		break;

	case FUSE_ACTION_REMOVE:
		iput(*backing_inode);
		*backing_inode = NULL;
		path_put(backing_path);
		*backing_path = (struct path) { };
		break;

	case FUSE_ACTION_REPLACE: {
		struct file *backing_file = feb->backing_file;

		if (!backing_file)
			return -EINVAL;
		if (IS_ERR(backing_file))
			return PTR_ERR(backing_file);

		if (backing_inode)
			iput(*backing_inode);
		*backing_inode = backing_file->f_inode;
		ihold(*backing_inode);

		path_put(backing_path);
		*backing_path = backing_file->f_path;
		path_get(backing_path);
		break;
	}

	default:
		return -EINVAL;
	}

	return 0;
}

int fuse_handle_bpf_prog(struct fuse_entry_bpf *feb, struct inode *parent,
			 struct bpf_prog **bpf)
{
	struct bpf_prog *new_bpf = NULL;

	switch (feb->out.bpf_action) {
	case FUSE_ACTION_KEEP: {
		/* Parent isn't presented, but we want to keep
		 * Don't touch bpf program at all in this case
		 */
		if (!parent)
			return 0;

		new_bpf = get_fuse_inode(parent)->bpf;
		if (new_bpf)
			bpf_prog_inc(new_bpf);
		break;
	}

	case FUSE_ACTION_REMOVE:
		break;

	case FUSE_ACTION_REPLACE: {
		struct file *bpf_file = feb->bpf_file;

		if (!bpf_file)
			return -EINVAL;
		if (IS_ERR(bpf_file))
			return PTR_ERR(bpf_file);

		new_bpf = fuse_get_bpf_prog(bpf_file);
		if (IS_ERR(new_bpf))
			return PTR_ERR(new_bpf);
		break;
	}

	default:
		return -EINVAL;
	}

	/* Cannot change existing program */
	if (*bpf) {
		if (new_bpf)
			bpf_prog_put(new_bpf);
		return new_bpf == *bpf ? 0 : -EINVAL;
	}

	*bpf = new_bpf;
	return 0;
}

struct dentry *fuse_lookup_finalize(struct fuse_bpf_args *fa, struct inode *dir,
			   struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_entry;
	struct dentry *backing_entry;
	struct inode *inode = NULL, *backing_inode;
	struct inode *entry_inode = entry->d_inode;
	struct fuse_entry_out *feo = fa->out_args[0].value;
	struct fuse_entry_bpf_out *febo = fa->out_args[1].value;
	struct fuse_entry_bpf *feb = container_of(febo, struct fuse_entry_bpf,
						  out);
	int error = -1;
	u64 target_nodeid = 0;
	struct dentry *ret = NULL;

	fuse_entry = get_fuse_dentry(entry);
	if (!fuse_entry) {
		ret = ERR_PTR(-EIO);
		goto out;
	}

	backing_entry = fuse_entry->backing_path.dentry;
	if (!backing_entry) {
		ret = ERR_PTR(-ENOENT);
		goto out;
	}

	if (entry_inode)
		target_nodeid = get_fuse_inode(entry_inode)->nodeid;

	backing_inode = backing_entry->d_inode;
	if (backing_inode)
		inode = fuse_iget_backing(dir->i_sb, target_nodeid,
					  backing_inode);

	error = inode ?
		fuse_handle_bpf_prog(feb, dir, &get_fuse_inode(inode)->bpf) :
		fuse_handle_bpf_prog(feb, dir, &fuse_entry->bpf);
	if (error) {
		ret = ERR_PTR(error);
		goto out;
	}

	if (inode) {
		error = fuse_handle_backing(feb,
					&get_fuse_inode(inode)->backing_inode,
					&fuse_entry->backing_path);
		if (error) {
			ret = ERR_PTR(error);
			goto out;
		}

		get_fuse_inode(inode)->nodeid = feo->nodeid;
		ret = d_splice_alias(inode, entry);
		if (!IS_ERR(ret))
			inode = NULL;
	}
out:
	iput(inode);
	if (feb->backing_file)
		fput(feb->backing_file);
	return ret;
}

int fuse_revalidate_backing(struct dentry *entry, unsigned int flags)
{
	struct fuse_dentry *fuse_dentry = get_fuse_dentry(entry);
	struct dentry *backing_entry = fuse_dentry->backing_path.dentry;

	spin_lock(&backing_entry->d_lock);
	if (d_unhashed(backing_entry)) {
		spin_unlock(&backing_entry->d_lock);
			return 0;
	}
	spin_unlock(&backing_entry->d_lock);

	if (unlikely(backing_entry->d_flags & DCACHE_OP_REVALIDATE))
		return backing_entry->d_op->d_revalidate(backing_entry, flags);
	return 1;
}

int fuse_canonical_path_initialize(struct fuse_bpf_args *fa,
				   struct fuse_dummy_io *fdi,
				   const struct path *path,
				   struct path *canonical_path)
{
	fa->opcode = FUSE_CANONICAL_PATH;
	return 0;
}

int fuse_canonical_path_backing(struct fuse_bpf_args *fa, const struct path *path,
				struct path *canonical_path)
{
	get_fuse_backing_path(path->dentry, canonical_path);
	return 0;
}

void *fuse_canonical_path_finalize(struct fuse_bpf_args *fa,
				   const struct path *path,
				   struct path *canonical_path)
{
	return NULL;
}

int fuse_mknod_initialize(
		struct fuse_bpf_args *fa, struct fuse_mknod_in *fmi,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev)
{
	*fmi = (struct fuse_mknod_in) {
		.mode = mode,
		.rdev = new_encode_dev(rdev),
		.umask = current_umask(),
	};
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(dir),
		.opcode = FUSE_MKNOD,
		.in_numargs = 2,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fmi),
			.value = fmi,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
	};

	return 0;
}

int fuse_mknod_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev)
{
	int err = 0;
	const struct fuse_mknod_in *fmi = fa->in_args[0].value;
	struct fuse_inode *fuse_inode = get_fuse_inode(dir);
	struct inode *backing_inode = fuse_inode->backing_inode;
	struct path backing_path = {};
	struct inode *inode = NULL;

	//TODO Actually deal with changing the backing entry in mknod
	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	mode = fmi->mode;
	if (!IS_POSIXACL(backing_inode))
		mode &= ~fmi->umask;
	err = vfs_mknod(&nop_mnt_idmap, backing_inode, backing_path.dentry, mode,
	                new_decode_dev(fmi->rdev));
	inode_unlock(backing_inode);
	if (err)
		goto out;
	if (d_really_is_negative(backing_path.dentry) ||
		unlikely(d_unhashed(backing_path.dentry))) {
		err = -EINVAL;
		/**
		 * TODO: overlayfs responds to this situation with a
		 * lookupOneLen. Should we do that too?
		 */
		goto out;
	}
	inode = fuse_iget_backing(dir->i_sb, fuse_inode->nodeid, backing_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	d_instantiate(entry, inode);
out:
	path_put(&backing_path);
	return err;
}

void *fuse_mknod_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode, dev_t rdev)
{
	return NULL;
}

int fuse_mkdir_initialize(
		struct fuse_bpf_args *fa, struct fuse_mkdir_in *fmi,
		struct inode *dir, struct dentry *entry, umode_t mode)
{
	*fmi = (struct fuse_mkdir_in) {
		.mode = mode,
		.umask = current_umask(),
	};
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(dir),
		.opcode = FUSE_MKDIR,
		.in_numargs = 2,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fmi),
			.value = fmi,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
	};

	return 0;
}

int fuse_mkdir_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir_inode, struct dentry *entry, umode_t mode)
{
	int err = 0;
	const struct fuse_mkdir_in *fmi = fa->in_args[0].value;
	struct fuse_inode *dir_fuse_inode = get_fuse_inode(dir_inode);
	struct inode *dir_backing_inode = dir_fuse_inode->backing_inode;
	struct path backing_path = {};
	struct inode *inode = NULL;

	//TODO Actually deal with changing the backing entry in mkdir
	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	inode_lock_nested(dir_backing_inode, I_MUTEX_PARENT);
	mode = fmi->mode;
	if (!IS_POSIXACL(dir_backing_inode))
		mode &= ~fmi->umask;
	err = vfs_mkdir(&nop_mnt_idmap, dir_backing_inode, backing_path.dentry, mode);
	if (err)
		goto out;
	if (d_really_is_negative(backing_path.dentry) ||
		unlikely(d_unhashed(backing_path.dentry))) {
		struct dentry *d = lookup_one_len(entry->d_name.name,
					backing_path.dentry->d_parent,
					entry->d_name.len);

		if (IS_ERR(d)) {
			err = PTR_ERR(d);
			goto out;
		}
		dput(backing_path.dentry);
		backing_path.dentry = d;
	}
	inode = fuse_iget_backing(dir_inode->i_sb, 0,
				  backing_path.dentry->d_inode);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out;
	}
	d_instantiate(entry, inode);
	if (get_fuse_inode(inode)->bpf)
		bpf_prog_put(get_fuse_inode(inode)->bpf);
	get_fuse_inode(inode)->bpf = get_fuse_dentry(entry)->bpf;
	get_fuse_dentry(entry)->bpf = NULL;
out:
	inode_unlock(dir_backing_inode);
	path_put(&backing_path);
	return err;
}

void *fuse_mkdir_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry, umode_t mode)
{
	return NULL;
}

int fuse_rmdir_initialize(
		struct fuse_bpf_args *fa, struct fuse_dummy_io *dummy,
		struct inode *dir, struct dentry *entry)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(dir),
		.opcode = FUSE_RMDIR,
		.in_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
	};

	return 0;
}

int fuse_rmdir_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry)
{
	int err = 0;
	struct path backing_path = {};
	struct dentry *backing_parent_dentry;
	struct inode *backing_inode;

	/* TODO Actually deal with changing the backing entry in rmdir */
	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	/* TODO Not sure if we should reverify like overlayfs, or get inode from d_parent */
	backing_parent_dentry = dget_parent(backing_path.dentry);
	backing_inode = d_inode(backing_parent_dentry);

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	err = vfs_rmdir(&nop_mnt_idmap, backing_inode, backing_path.dentry);
	inode_unlock(backing_inode);

	dput(backing_parent_dentry);
	if (!err)
		d_drop(entry);
	path_put(&backing_path);
	return err;
}

void *fuse_rmdir_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry)
{
	return NULL;
}

static int fuse_rename_backing_common(
			 struct inode *olddir, struct dentry *oldent,
			 struct inode *newdir, struct dentry *newent,
			 unsigned int flags)
{
	int err = 0;
	struct path old_backing_path;
	struct path new_backing_path;
	struct dentry *old_backing_dir_dentry;
	struct dentry *old_backing_dentry;
	struct dentry *new_backing_dir_dentry;
	struct dentry *new_backing_dentry;
	struct dentry *trap = NULL;
	struct inode *target_inode;
	struct renamedata rd;

	//TODO Actually deal with changing anything that isn't a flag
	get_fuse_backing_path(oldent, &old_backing_path);
	if (!old_backing_path.dentry)
		return -EBADF;
	get_fuse_backing_path(newent, &new_backing_path);
	if (!new_backing_path.dentry) {
		/*
		 * TODO A file being moved from a backing path to another
		 * backing path which is not yet instrumented with FUSE-BPF.
		 * This may be slow and should be substituted with something
		 * more clever.
		 */
		err = -EXDEV;
		goto put_old_path;
	}
	if (new_backing_path.mnt != old_backing_path.mnt) {
		err = -EXDEV;
		goto put_new_path;
	}
	old_backing_dentry = old_backing_path.dentry;
	new_backing_dentry = new_backing_path.dentry;
	old_backing_dir_dentry = dget_parent(old_backing_dentry);
	new_backing_dir_dentry = dget_parent(new_backing_dentry);
	target_inode = d_inode(newent);

	trap = lock_rename(old_backing_dir_dentry, new_backing_dir_dentry);
	if (trap == old_backing_dentry) {
		err = -EINVAL;
		goto put_parents;
	}
	if (trap == new_backing_dentry) {
		err = -ENOTEMPTY;
		goto put_parents;
	}
	rd = (struct renamedata) {
		.old_mnt_idmap = &nop_mnt_idmap,
		.old_dir = d_inode(old_backing_dir_dentry),
		.old_dentry = old_backing_dentry,
		.new_mnt_idmap = &nop_mnt_idmap,
		.new_dir = d_inode(new_backing_dir_dentry),
		.new_dentry = new_backing_dentry,
		.flags = flags,
	};
	err = vfs_rename(&rd);
	if (err)
		goto unlock;
	if (target_inode)
		fsstack_copy_attr_all(target_inode,
				get_fuse_inode(target_inode)->backing_inode);
	fsstack_copy_attr_all(d_inode(oldent), d_inode(old_backing_dentry));
unlock:
	unlock_rename(old_backing_dir_dentry, new_backing_dir_dentry);
put_parents:
	dput(new_backing_dir_dentry);
	dput(old_backing_dir_dentry);
put_new_path:
	path_put(&new_backing_path);
put_old_path:
	path_put(&old_backing_path);
	return err;
}

int fuse_rename2_initialize(struct fuse_bpf_args *fa, struct fuse_rename2_in *fri,
			    struct inode *olddir, struct dentry *oldent,
			    struct inode *newdir, struct dentry *newent,
			    unsigned int flags)
{
	*fri = (struct fuse_rename2_in) {
		.newdir = get_node_id(newdir),
		.flags = flags,
	};
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(olddir),
		.opcode = FUSE_RENAME2,
		.in_numargs = 3,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fri),
			.value = fri,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = oldent->d_name.len + 1,
			.value = oldent->d_name.name,
		},
		.in_args[2] = (struct fuse_bpf_in_arg) {
			.size = newent->d_name.len + 1,
			.value = newent->d_name.name,
		},
	};

	return 0;
}

int fuse_rename2_backing(struct fuse_bpf_args *fa,
			 struct inode *olddir, struct dentry *oldent,
			 struct inode *newdir, struct dentry *newent,
			 unsigned int flags)
{
	const struct fuse_rename2_in *fri = fa->in_args[0].value;

	/* TODO: deal with changing dirs/ents */
	return fuse_rename_backing_common(olddir, oldent, newdir, newent, fri->flags);
}

void *fuse_rename2_finalize(struct fuse_bpf_args *fa,
			    struct inode *olddir, struct dentry *oldent,
			    struct inode *newdir, struct dentry *newent,
			    unsigned int flags)
{
	return NULL;
}

int fuse_rename_initialize(struct fuse_bpf_args *fa, struct fuse_rename_in *fri,
			   struct inode *olddir, struct dentry *oldent,
			   struct inode *newdir, struct dentry *newent)
{
	*fri = (struct fuse_rename_in) {
		.newdir = get_node_id(newdir),
	};
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(olddir),
		.opcode = FUSE_RENAME,
		.in_numargs = 3,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(*fri),
			.value = fri,
		},
		.in_args[1] = (struct fuse_bpf_in_arg) {
			.size = oldent->d_name.len + 1,
			.value = oldent->d_name.name,
		},
		.in_args[2] = (struct fuse_bpf_in_arg) {
			.size = newent->d_name.len + 1,
			.value = newent->d_name.name,
		},
	};

	return 0;
}

int fuse_rename_backing(struct fuse_bpf_args *fa,
			struct inode *olddir, struct dentry *oldent,
			struct inode *newdir, struct dentry *newent)
{
	/* TODO: deal with changing dirs/ents */
	return fuse_rename_backing_common(olddir, oldent, newdir, newent, 0);
}

void *fuse_rename_finalize(struct fuse_bpf_args *fa,
			   struct inode *olddir, struct dentry *oldent,
			   struct inode *newdir, struct dentry *newent)
{
	return NULL;
}

int fuse_unlink_initialize(
		struct fuse_bpf_args *fa, struct fuse_dummy_io *dummy,
		struct inode *dir, struct dentry *entry)
{
	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(dir),
		.opcode = FUSE_UNLINK,
		.in_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = entry->d_name.len + 1,
			.value = entry->d_name.name,
		},
	};

	return 0;
}

int fuse_unlink_backing(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry)
{
	int err = 0;
	struct path backing_path = {};
	struct dentry *backing_parent_dentry;
	struct inode *backing_inode;

	/* TODO Actually deal with changing the backing entry in unlink */
	get_fuse_backing_path(entry, &backing_path);
	if (!backing_path.dentry)
		return -EBADF;

	/* TODO Not sure if we should reverify like overlayfs, or get inode from d_parent */
	backing_parent_dentry = dget_parent(backing_path.dentry);
	backing_inode = d_inode(backing_parent_dentry);

	inode_lock_nested(backing_inode, I_MUTEX_PARENT);
	err = vfs_unlink(&nop_mnt_idmap, backing_inode, backing_path.dentry, NULL);
	inode_unlock(backing_inode);

	dput(backing_parent_dentry);
	if (!err)
		d_drop(entry);
	path_put(&backing_path);
	return err;
}

void *fuse_unlink_finalize(
		struct fuse_bpf_args *fa,
		struct inode *dir, struct dentry *entry)
{
	return NULL;
}

int fuse_link_initialize(struct fuse_bpf_args *fa, struct fuse_link_in *fli,
			 struct dentry *entry, struct inode *dir,
			 struct dentry *newent)
{
	struct inode *src_inode = entry->d_inode;

	*fli = (struct fuse_link_in){
		.oldnodeid = get_node_id(src_inode),
	};

	fa->opcode = FUSE_LINK;
	fa->in_numargs = 2;
	fa->in_args[0].size = sizeof(*fli);
	fa->in_args[0].value = fli;
	fa->in_args[1].size = newent->d_name.len + 1;
	fa->in_args[1].value = newent->d_name.name;

	return 0;
}

int fuse_link_backing(struct fuse_bpf_args *fa, struct dentry *entry,
		      struct inode *dir, struct dentry *newent)
{
	int err = 0;
	struct path backing_old_path = {};
	struct path backing_new_path = {};
	struct dentry *backing_dir_dentry;
	struct inode *fuse_new_inode = NULL;
	struct fuse_inode *fuse_dir_inode = get_fuse_inode(dir);
	struct inode *backing_dir_inode = fuse_dir_inode->backing_inode;

	get_fuse_backing_path(entry, &backing_old_path);
	if (!backing_old_path.dentry)
		return -EBADF;

	get_fuse_backing_path(newent, &backing_new_path);
	if (!backing_new_path.dentry) {
		err = -EBADF;
		goto err_dst_path;
	}

	backing_dir_dentry = dget_parent(backing_new_path.dentry);
	backing_dir_inode = d_inode(backing_dir_dentry);

	inode_lock_nested(backing_dir_inode, I_MUTEX_PARENT);
	err = vfs_link(backing_old_path.dentry, &nop_mnt_idmap,
		       backing_dir_inode, backing_new_path.dentry, NULL);
	inode_unlock(backing_dir_inode);
	if (err)
		goto out;

	if (d_really_is_negative(backing_new_path.dentry) ||
	    unlikely(d_unhashed(backing_new_path.dentry))) {
		err = -EINVAL;
		/**
		 * TODO: overlayfs responds to this situation with a
		 * lookupOneLen. Should we do that too?
		 */
		goto out;
	}

	fuse_new_inode = fuse_iget_backing(dir->i_sb, fuse_dir_inode->nodeid, backing_dir_inode);
	if (IS_ERR(fuse_new_inode)) {
		err = PTR_ERR(fuse_new_inode);
		goto out;
	}
	d_instantiate(newent, fuse_new_inode);

out:
	dput(backing_dir_dentry);
	path_put(&backing_new_path);
err_dst_path:
	path_put(&backing_old_path);
	return err;
}

void *fuse_link_finalize(struct fuse_bpf_args *fa, struct dentry *entry,
			 struct inode *dir, struct dentry *newent)
{
	return NULL;
}

int fuse_getattr_initialize(struct fuse_bpf_args *fa, struct fuse_getattr_io *fgio,
			const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	fgio->fgi = (struct fuse_getattr_in) {
		.getattr_flags = flags,
		.fh = -1, /* TODO is this OK? */
	};

	fgio->fao = (struct fuse_attr_out) {0};

	*fa = (struct fuse_bpf_args) {
		.nodeid = get_node_id(entry->d_inode),
		.opcode = FUSE_GETATTR,
		.in_numargs = 1,
		.out_numargs = 1,
		.in_args[0] = (struct fuse_bpf_in_arg) {
			.size = sizeof(fgio->fgi),
			.value = &fgio->fgi,
		},
		.out_args[0] = (struct fuse_bpf_arg) {
			.size = sizeof(fgio->fao),
			.value = &fgio->fao,
		},
	};

	return 0;
}

static void fuse_stat_to_attr(struct fuse_conn *fc, struct inode *inode,
		struct kstat *stat, struct fuse_attr *attr)
{
	unsigned int blkbits;

	/* see the comment in fuse_change_attributes() */
	if (fc->writeback_cache && S_ISREG(inode->i_mode)) {
		stat->size = i_size_read(inode);
		stat->mtime.tv_sec = inode_get_mtime_sec(inode);;
		stat->mtime.tv_nsec = inode_get_mtime_nsec(inode);
		stat->ctime.tv_sec = inode_get_ctime_sec(inode);
		stat->ctime.tv_nsec = inode_get_ctime_nsec(inode);
	}

	attr->ino = stat->ino;
	attr->mode = (inode->i_mode & S_IFMT) | (stat->mode & 07777);
	attr->nlink = stat->nlink;
	attr->uid = from_kuid(fc->user_ns, stat->uid);
	attr->gid = from_kgid(fc->user_ns, stat->gid);
	attr->atime = stat->atime.tv_sec;
	attr->atimensec = stat->atime.tv_nsec;
	attr->mtime = stat->mtime.tv_sec;
	attr->mtimensec = stat->mtime.tv_nsec;
	attr->ctime = stat->ctime.tv_sec;
	attr->ctimensec = stat->ctime.tv_nsec;
	attr->size = stat->size;
	attr->blocks = stat->blocks;

	if (stat->blksize != 0)
		blkbits = ilog2(stat->blksize);
	else
		blkbits = inode->i_sb->s_blocksize_bits;

	attr->blksize = 1 << blkbits;
}

int fuse_getattr_backing(struct fuse_bpf_args *fa,
		const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	struct path *backing_path =
		&get_fuse_dentry(entry)->backing_path;
	struct inode *backing_inode = backing_path->dentry->d_inode;
	struct fuse_attr_out *fao = fa->out_args[0].value;
	struct kstat tmp;
	int err;

	if (!stat)
		stat = &tmp;

	if (flags & AT_GETATTR_NOSEC)
		err = vfs_getattr_nosec(backing_path, stat, request_mask, flags);
	else
		err = vfs_getattr(backing_path, stat, request_mask, flags);

	if (!err)
		fuse_stat_to_attr(get_fuse_conn(entry->d_inode),
				  backing_inode, stat, &fao->attr);

	return err;
}

void *fuse_getattr_finalize(struct fuse_bpf_args *fa,
			const struct dentry *entry, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	struct fuse_attr_out *outarg = fa->out_args[0].value;
	struct inode *inode = entry->d_inode;
	u64 attr_version = fuse_get_attr_version(get_fuse_mount(inode)->fc);
	int err = 0;

	/* TODO: Ensure this doesn't happen if we had an error getting attrs in
	 * backing.
	 */
	err = finalize_attr(inode, outarg, attr_version, stat);
	return ERR_PTR(err);
}

static void fattr_to_iattr(struct fuse_conn *fc,
			   const struct fuse_setattr_in *arg,
			   struct iattr *iattr)
{
	unsigned int fvalid = arg->valid;

	if (fvalid & FATTR_MODE)
		iattr->ia_valid |= ATTR_MODE, iattr->ia_mode = arg->mode;
	if (fvalid & FATTR_UID) {
		iattr->ia_valid |= ATTR_UID;
		iattr->ia_uid = make_kuid(fc->user_ns, arg->uid);
	}
	if (fvalid & FATTR_GID) {
		iattr->ia_valid |= ATTR_GID;
		iattr->ia_gid = make_kgid(fc->user_ns, arg->gid);
	}
	if (fvalid & FATTR_SIZE)
		iattr->ia_valid |= ATTR_SIZE,  iattr->ia_size = arg->size;
	if (fvalid & FATTR_ATIME) {
		iattr->ia_valid |= ATTR_ATIME;
		iattr->ia_atime.tv_sec = arg->atime;
		iattr->ia_atime.tv_nsec = arg->atimensec;
		if (!(fvalid & FATTR_ATIME_NOW))
			iattr->ia_valid |= ATTR_ATIME_SET;
	}
	if (fvalid & FATTR_MTIME) {
		iattr->ia_valid |= ATTR_MTIME;
		iattr->ia_mtime.tv_sec = arg->mtime;
		iattr->ia_mtime.tv_nsec = arg->mtimensec;
		if (!(fvalid & FATTR_MTIME_NOW))
			iattr->ia_valid |= ATTR_MTIME_SET;
	}
	if (fvalid & FATTR_CTIME) {
		iattr->ia_valid |= ATTR_CTIME;
		iattr->ia_ctime.tv_sec = arg->ctime;
		iattr->ia_ctime.tv_nsec = arg->ctimensec;
	}
}

int fuse_setattr_initialize(struct fuse_bpf_args *fa, struct fuse_setattr_io *fsio,
		struct dentry *dentry, struct iattr *attr, struct file *file)
{
	struct fuse_conn *fc = get_fuse_conn(dentry->d_inode);

	*fsio = (struct fuse_setattr_io) {0};
	iattr_to_fattr(&nop_mnt_idmap, fc, attr, &fsio->fsi, true);

	*fa = (struct fuse_bpf_args) {
		.opcode = FUSE_SETATTR,
		.nodeid = get_node_id(dentry->d_inode),
		.in_numargs = 1,
		.in_args[0].size = sizeof(fsio->fsi),
		.in_args[0].value = &fsio->fsi,
		.out_numargs = 1,
		.out_args[0].size = sizeof(fsio->fao),
		.out_args[0].value = &fsio->fao,
	};

	return 0;
}

int fuse_setattr_backing(struct fuse_bpf_args *fa,
		struct dentry *dentry, struct iattr *attr, struct file *file)
{
	struct fuse_conn *fc = get_fuse_conn(dentry->d_inode);
	const struct fuse_setattr_in *fsi = fa->in_args[0].value;
	struct iattr new_attr = {0};
	struct path *backing_path = &get_fuse_dentry(dentry)->backing_path;
>>>>>>> CHANGE (5c3d7b8761264e2a2deb121020bb933273b240ba ANDROID: fuse: Use vma_set_file() in fuse_backing_mmap())
	int res;

	pr_debug("%s: fd=%d flags=0x%x\n", __func__, map->fd, map->flags);

	/* TODO: relax CAP_SYS_ADMIN once backing files are visible to lsof */
	/* Android already restricts access here, and we don't want to grant extra
	 * Permissions to the daemon */
#if 0
	res = -EPERM;
	if (!fc->passthrough || !capable(CAP_SYS_ADMIN))
		goto out;
#endif

	res = -EINVAL;
	if (map->flags || map->padding)
		goto out;

	file = fget_raw(map->fd);
	res = -EBADF;
	if (!file)
		goto out;

	/* read/write/splice/mmap passthrough only relevant for regular files */
	res = d_is_dir(file->f_path.dentry) ? -EISDIR : -EINVAL;
	if (!d_is_reg(file->f_path.dentry))
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
	/* Android already restricts access here, and we don't want to grant extra
	 * Permissions to the daemon */
#if 0
	err = -EPERM;
	if (!fc->passthrough || !capable(CAP_SYS_ADMIN))
		goto out;
#endif

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

struct fuse_backing *fuse_backing_lookup(struct fuse_conn *fc, int backing_id)
{
	struct fuse_backing *fb;

	rcu_read_lock();
	fb = idr_find(&fc->backing_files_map, backing_id);
	fb = fuse_backing_get(fb);
	rcu_read_unlock();

	return fb;
}
