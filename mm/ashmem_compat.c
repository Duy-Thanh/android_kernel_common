// SPDX-License-Identifier: GPL-2.0

/*
 * Ashmem compatability for memfd
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Carlos Galo <carlosgalo@google.com>
 */

#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/memfd.h>
#include <linux/mm_types.h>
#include <linux/mman.h>
#include <linux/shmem_fs.h>
#include <linux/spinlock.h>

#include "../drivers/staging/android/uapi/ashmem.h"
#include "ashmem_compat.h"

#define ASHMEM_COMPAT_COOKIE 0xcafebeef
#define MFD_NAME_PREFIX "memfd:"

static DEFINE_SPINLOCK(fops_lock);
static struct file_operations ashmem_compat_fops;
static int (*shmem_mmap)(struct file *, struct vm_area_struct *);

static int ashmem_compat_set_name(struct file *file, void __user *name)
{
	struct inode *inode = file_inode(file);
	char local_name [ASHMEM_NAME_LEN];
	char *final_name;
	int len;
	int ret = 0;
	size_t buf_len;

	len = strncpy_from_user(local_name, name, ASHMEM_NAME_LEN);
	if (len < 0)
		return len;

	buf_len = strlen(MFD_NAME_PREFIX) + len + 1;
	final_name = kmalloc(buf_len, GFP_KERNEL);
	if (!final_name) {
		ret = -ENOMEM;
		goto out;
	}
	snprintf(final_name, buf_len, "%s%s", MFD_NAME_PREFIX, local_name);

	inode_lock(inode);
	if (file->f_path.dentry->d_fsdata)
		kfree(file->f_path.dentry->d_fsdata);
	file->f_path.dentry->d_fsdata = final_name;
	inode_unlock(inode);
out:
	return ret;
}

static int ashmem_compat_get_name(struct file *file, void __user *name)
{
	struct inode *inode = file_inode(file);
	char *filename;
	int ret;

	inode_lock_shared(inode);
	if (file->f_path.dentry->d_fsdata)
		filename = file->f_path.dentry->d_fsdata;
	else
		filename = (char *)file->f_path.dentry->d_name.name;
	/*
	 * memfd files have names of the following format: "memfd:name", where name
	 * is the name argument that is provided to memfd_create() or with ASHMEM_SET_NAME.
	 *
	 * To retain compatibility with ashmem's behavior, just return "name" instead of
	 * "memfd:name".
	 */
	filename += strlen("memfd:");

	if (copy_to_user(name, filename, ASHMEM_NAME_LEN))
		ret = -EFAULT;
	else
		ret = 0;
	inode_unlock_shared(inode);

	return ret;
}

static int ashmem_compat_set_size(struct file* file, size_t size)
{
	struct inode *inode = file_inode(file);
	unsigned int file_seals;
	int ret = 0;

	/*
	 * Synchronize against updates to the file seals from mmap(). mmap() will be responsible
	 * for sealing the file against any new seals being added, as well as sealing the file size.
	 *
	 * Making the size update atomic with respect to mmap() prevents the file size from changing
	 * after the file has been mapped.
	 */
	inode_lock(inode);

	file_seals = memfd_fcntl(file, F_GET_SEALS, 0);

	if (!(file_seals & (F_SEAL_GROW | F_SEAL_SHRINK)))
		i_size_write(inode, size);
	else
		ret = -EINVAL;

	inode_unlock(inode);

	return ret;
}

static u64 ashmem_compat_get_size(struct file *file)
{
	struct inode *inode = file_inode(file);
	u64 size;

	inode_lock_shared(inode);

	size = i_size_read(inode);

	inode_unlock_shared(inode);

	return size;
}

/* Assumes that the file's inode semaphore is held */
static unsigned long __ashmem_compat_get_prot_mask(struct file *file)
{
	unsigned long prot_mask = PROT_READ | PROT_EXEC;
	long file_seals;

	file_seals = memfd_fcntl(file, F_GET_SEALS, 0);

	if (!(file_seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)))
		prot_mask |= PROT_WRITE;

	return prot_mask;
}

static int ashmem_compat_set_prot_mask(struct file *file, unsigned long prot)
{
	struct inode *inode;
	unsigned int *file_seals_ptr;
	unsigned long current_prot_mask;
	int ret = 0;

	inode = file_inode(file);
	inode_lock(inode);

	/* Retain behavior of only allowing protections to be removed. */
	current_prot_mask = __ashmem_compat_get_prot_mask(file);
	if ((current_prot_mask & prot) != prot) {
		ret = -EINVAL;
		goto out;
	}

	if (current->personality & READ_IMPLIES_EXEC)
		prot |= PROT_EXEC;

	/*
	 * Ashmem buffers by default start with PROT_READ | PROT_WRITE | PROT_EXEC permissions.
	 * This ioctl command was used to remove permissions from the buffer's permission set.
	 *
	 * Removing PROT_READ:
	 *
	 * Given that memfd buffers are always readable, it doesn't make sense to remove that
	 * ability so that is not supported.
	 *
	 * Removing PROT_EXEC:
	 *
	 * Unknown.
	 *
	 * Removing PROT_WRITE:
	 *
	 * We can prevent any other mappings from having write permissions by adding the
	 * F_SEAL_WRITE mapping. However, that would conflict with known usecases where it is
	 * desirable to maintain an existing writable mapping, but forbid future writable mappings.
	 *
	 * To support that usecase, we use F_SEAL_FUTURE_WRITE.
	 */
	file_seals_ptr = memfd_file_seals_ptr(file);

	if (!(prot & PROT_WRITE))
		*file_seals_ptr |= F_SEAL_FUTURE_WRITE;

out:
	inode_unlock(inode);
	return ret;
}

static unsigned long ashmem_compat_get_prot_mask(struct file *file)
{
	struct inode *inode = file_inode(file);
	unsigned long prot_mask;

	inode_lock_shared(inode);
	prot_mask = __ashmem_compat_get_prot_mask(file);
	inode_unlock_shared(inode);
	return prot_mask;
}

static long ashmem_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOTTY;
	unsigned long inode_nr;

	switch (cmd) {
	case ASHMEM_SET_NAME:
		ret = ashmem_compat_set_name(file, (void __user *)arg);
		break;
	case ASHMEM_GET_NAME:
		ret = ashmem_compat_get_name(file, (void __user *)arg);
		break;
	case ASHMEM_SET_SIZE:
		ret = ashmem_compat_set_size(file,(size_t)arg);
		break;
	case ASHMEM_GET_SIZE:
		ret = ashmem_compat_get_size(file);
		break;
	case ASHMEM_SET_PROT_MASK:
		ret = ashmem_compat_set_prot_mask(file, arg);
		break;
	case ASHMEM_GET_PROT_MASK:
		ret = ashmem_compat_get_prot_mask(file);
		break;
	case ASHMEM_PIN:
		ret = ASHMEM_NOT_PURGED;
		break;
	case ASHMEM_UNPIN:
		ret = 0;
		break;
	case ASHMEM_GET_PIN_STATUS:
		ret = ASHMEM_IS_PINNED;
		break;
	case ASHMEM_PURGE_ALL_CACHES:
		ret = -EPERM;
		if (capable(CAP_SYS_ADMIN))
			ret = 0;
		break;
	case ASHMEM_GET_FILE_ID:
		inode_nr = file_inode(file)->i_ino;

		if (copy_to_user((void __user *)arg, &inode_nr , sizeof(inode_nr)))
			ret = -EFAULT;
		else
			ret = 0;
		break;
	}

	return ret;
}

static unsigned long ashmem_compat_get_unmapped_area(struct file *file, unsigned long addr,
						     unsigned long len, unsigned long pgoff,
						     unsigned long flags)
{
	return mm_get_unmapped_area(current->mm, file, addr, len, pgoff, flags);
}

static bool is_ashmem_compatible(struct file *file)
{
	return file->private_data == (void *)ASHMEM_COMPAT_COOKIE;
}

static int __ashmem_compat_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode;
	loff_t size;
	unsigned int *file_seals_ptr;
	int ret;

	inode = file_inode(file);
	inode_lock(inode);

	/* Caller must set size before mapping. */
	size = i_size_read(inode);
	if (size <= 0) {
		ret = -EINVAL;
		goto out;
	}

	if (vma->vm_end - vma->vm_start > PAGE_ALIGN(size)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Uses seal_check_write() to ensure that the mapping cannot be made writable with
	 * mprotect() later on.
	 */
	ret = shmem_mmap(file, vma);
	if(!ret) {
		file_seals_ptr = memfd_file_seals_ptr(file);
		if (!(*file_seals_ptr & (F_SEAL_SHRINK | F_SEAL_GROW)))
			*file_seals_ptr |= F_SEAL_SHRINK | F_SEAL_GROW;
	}
out:
	inode_unlock(inode);
	return ret;
}

static int ashmem_compat_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (is_ashmem_compatible(file))
		return __ashmem_compat_mmap(file, vma);

	return shmem_mmap(file, vma);
}

static char *ashmem_compat_dname(struct dentry *dentry, char *buffer, int buflen)
{
	ssize_t ret = 0;
	struct inode *inode = d_inode(dentry);
	char local_name[ASHMEM_NAME_LEN];
	char *filename;

	inode_lock_shared(inode);
	filename = dentry->d_fsdata ? (char *)dentry->d_fsdata : (char *)dentry->d_name.name;
	ret = strscpy(local_name, filename, sizeof(local_name));
	inode_unlock_shared(inode);

	return dynamic_dname(buffer, buflen, "/%s (deleted)", ret > 0 ? local_name : "");
}

static void ashmem_compat_release(struct dentry *dentry)
{
	kfree(dentry->d_fsdata);
}

static const struct dentry_operations ashmem_compat_dentry_ops = {
	.d_dname = ashmem_compat_dname,
	.d_release = ashmem_compat_release,
};

/*
 * install_ashmem_compat_fops - Change the fops for a memfd to ashmem_compat_fops.
 * @file: The memfd file structure.
 * @ashmem_compatible: Whether or not if ashmem compatibility checks should be applied to this file.
 */
void install_ashmem_compat_fops(struct file *file, bool ashmem_compatible)
{
	if (!file || !shmem_file(file))
		return;

	spin_lock(&fops_lock);
	if (!shmem_mmap) {
		ashmem_compat_fops = *file->f_op;
		shmem_mmap = file->f_op->mmap;

		ashmem_compat_fops.unlocked_ioctl = ashmem_compat_ioctl;
		/* Overwrite mmap() to be able to do additional checks that ashmem requires. */
		ashmem_compat_fops.mmap = ashmem_compat_mmap;
		/*
		 * If the get_unmapped_area() function pointer is not overwritten, then
		 * shmem_get_unmapped_area() will be invoked in the future.
		 *
		 * This is not desirable as, shmem_get_unmapped_area() will trigger a VM_BUG_ON()
		 * since file->f_op != shmem_file_operations.
		 */
		ashmem_compat_fops.get_unmapped_area = ashmem_compat_get_unmapped_area;
	}
	spin_unlock(&fops_lock);

	file->f_op = &ashmem_compat_fops;
	file->private_data = ashmem_compatible ? (void *)ASHMEM_COMPAT_COOKIE : NULL;
	d_set_d_op(file->f_path.dentry, &ashmem_compat_dentry_ops);
}
