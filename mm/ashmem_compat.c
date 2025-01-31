// SPDX-License-Identifier: GPL-2.0

/*
 * Ashmem compatability for memfd
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Carlos Galo <carlosgalo@google.com>
 */

#include <linux/ashmem_compat.h>
#include <linux/bitfield.h>
#include <linux/capability.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/memfd.h>
#include <linux/miscdevice.h>
#include <linux/mm_types.h>
#include <linux/mman.h>
#include <linux/shmem_fs.h>
#include <linux/spinlock.h>

#define ASHMEM_NAME_LEN		256

#define ASHMEM_NAME_DEF		"dev/ashmem"

/* Return values from ASHMEM_PIN: Was the mapping purged while unpinned? */
#define ASHMEM_NOT_PURGED	0
#define ASHMEM_WAS_PURGED	1

/* Return values from ASHMEM_GET_PIN_STATUS: Is the mapping pinned? */
#define ASHMEM_IS_UNPINNED	0
#define ASHMEM_IS_PINNED	1

struct ashmem_pin {
	__u32 offset;	/* offset into region, in bytes, page-aligned */
	__u32 len;	/* length forward from offset, in bytes, page-aligned */
};

#define __ASHMEMIOC		0x77

#define ASHMEM_SET_NAME		_IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME		_IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE		_IOW(__ASHMEMIOC, 3, size_t)
#define ASHMEM_GET_SIZE		_IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK	_IOW(__ASHMEMIOC, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK	_IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN		_IOW(__ASHMEMIOC, 7, struct ashmem_pin)
#define ASHMEM_UNPIN		_IOW(__ASHMEMIOC, 8, struct ashmem_pin)
#define ASHMEM_GET_PIN_STATUS	_IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES	_IO(__ASHMEMIOC, 10)
#define ASHMEM_GET_FILE_ID		_IOR(__ASHMEMIOC, 11, unsigned long)

#ifdef CONFIG_COMPAT
#define COMPAT_ASHMEM_SET_SIZE		_IOW(__ASHMEMIOC, 3, compat_size_t)
#define COMPAT_ASHMEM_SET_PROT_MASK	_IOW(__ASHMEMIOC, 5, unsigned int)
#endif

#define MFD_NAME_PREFIX "memfd:"
#define ASHMEM_COMPAT_PROT GENMASK(2, 0)
#define ASHMEM_COMPAT_MAPPED BIT(3)

static DEFINE_SPINLOCK(fops_lock);
static struct file_operations ashmem_compat_fops;

static bool is_ashmem_compat_file(struct file *file)
{
	return file->f_op == &ashmem_compat_fops;
}

static void set_buffer_mapped(struct file *file)
{
	unsigned long ashmem_compat_mask = (unsigned long)file->private_data;

	ashmem_compat_mask |= ASHMEM_COMPAT_MAPPED;
	file->private_data = (void *)ashmem_compat_mask;
}

static bool is_buffer_mapped(struct file *file)
{
	unsigned long ashmem_compat_mask = (unsigned long)file->private_data;

	return ashmem_compat_mask & ASHMEM_COMPAT_MAPPED;
}

static int ashmem_compat_set_name(struct file *file, void __user *name)
{
	struct inode *inode = file_inode(file);
	char local_name[ASHMEM_NAME_LEN];
	char *final_name;
	long len;
	int ret = 0;
	size_t buf_len;

	inode_lock(inode);

	/*
	 * Disallow changing the buffer name after it has been mapped to match the behavior of
	 * the ashmem driver.
	 */
	if (is_buffer_mapped(file)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * strncpy_from_user() either returns the amount of bytes copied, but not including the NUL
	 * terminating byte or ASHMEM_NAME_LEN if the string was too big.
	 */
	len = strncpy_from_user(local_name, name, ASHMEM_NAME_LEN);
	if (len <= 0) {
		ret = -EFAULT;
		goto out;
	} else if (len == ASHMEM_NAME_LEN) {
		/* Truncate the name to match the legacy behavior. */
		local_name[--len] = '\0';
	}

	buf_len = strlen(MFD_NAME_PREFIX) + len + 1;
	final_name = kmalloc(buf_len, GFP_KERNEL);
	if (!final_name) {
		ret = -ENOMEM;
		goto out;
	}
	scnprintf(final_name, buf_len, "%s%s", MFD_NAME_PREFIX, local_name);

	kfree(file->f_path.dentry->d_fsdata);
	file->f_path.dentry->d_fsdata = final_name;
out:
	inode_unlock(inode);
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
	filename += strlen(MFD_NAME_PREFIX);

	/*
	 * name is expected to be ASHMEM_NAME_LEN in size. If ASHMEM_SET_NAME was used, then the
	 * portion of the string containing the name should fit in the buffer.
	 *
	 * If ASHMEM_SET_NAME was not used, then the portion of the string containing the name
	 * will be at most MFD_NAME_MAX_LEN, which is less than ASHMEM_NAME_LEN, so that should
	 * still fit in the buffer.
	 *
	 * Add 1 to account for the NUL terminating byte.
	 */
	ret = copy_to_user(name, filename, strlen(filename) + 1) ? -EFAULT : 0;

	inode_unlock_shared(inode);
	return ret;
}

static int ashmem_compat_set_size(struct file* file, size_t size)
{
	struct inode *inode = file_inode(file);
	unsigned int *file_seals_ptr;
	unsigned int file_seals;
	int ret = 0;

	inode_lock(inode);

	/*
	 * Disallow changing the buffer size after it has been mapped to match the behavior of the
	 * ashmem driver.
	 */
	if (is_buffer_mapped(file)) {
		ret = -EINVAL;
		goto out;
	}

	file_seals_ptr = memfd_file_seals_ptr(file);
	file_seals = *file_seals_ptr;

	if (file_seals & (F_SEAL_GROW | F_SEAL_SHRINK)) {
		ret = -EINVAL;
		goto out;
	}

	i_size_write(inode, size);
	/*
	 * While ashmem's legacy behavior was to allow the buffer size to be changed until the
	 * first time it was mapped, emulating that behavior would mean doing that for all memfds,
	 * (i.e. mmap implies file seals for the size). Overloading mmap as such may not be ideal
	 * for all memfds, so we seal the file size here.
	 */
	*file_seals_ptr |= F_SEAL_GROW | F_SEAL_SHRINK;

out:
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

/* Assumes that the file's inode lock is held */
static unsigned long __ashmem_compat_get_prot_mask(struct file *file)
{
	unsigned int file_seals = memfd_fcntl(file, F_GET_SEALS, 0);
	unsigned long prot_mask = PROT_READ | PROT_EXEC;

	/* memfds are readable and executable by default. Only writability can be changed. */
	if (!(file_seals & (F_SEAL_WRITE | F_SEAL_FUTURE_WRITE)))
		prot_mask |= PROT_WRITE;

	return prot_mask;
}

static int ashmem_compat_set_prot_mask(struct file *file, unsigned long prot)
{
	struct inode *inode = file_inode(file);
	unsigned int *file_seals_ptr;
	unsigned long current_prot_mask;
	int ret = 0;

	inode_lock(inode);

	/*
	 * memfds are always readable and executable; there is no way to remove either mapping
	 * permission, nor is there a known usecase that requires it.
	 *
	 * Attempting to remove either of these mapping permissions will return successfully, but
	 * will be a nop, as the buffer will still be mappable with these permissions.
	 */
	prot |= PROT_READ | PROT_EXEC;

	/* Retain behavior of only allowing protections to be removed. */
	current_prot_mask = __ashmem_compat_get_prot_mask(file);
	if ((current_prot_mask & prot) != prot) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Ashmem buffers by default start with PROT_READ | PROT_WRITE | PROT_EXEC permissions.
	 * This ioctl command was used to remove permissions from the buffer's permission set.
	 *
	 * Removing PROT_WRITE:
	 *
	 * We can prevent any other mappings from having write permissions by adding the
	 * F_SEAL_WRITE mapping. However, that would conflict with known usecases where it is
	 * desirable to maintain an existing writable mapping, but forbid future writable mappings.
	 *
	 * To support that usecase, we use F_SEAL_FUTURE_WRITE.
	 */
	if (!(prot & PROT_WRITE)) {
		file_seals_ptr = memfd_file_seals_ptr(file);
		*file_seals_ptr |= F_SEAL_FUTURE_WRITE;
	}
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

static void ashmem_compat_dentry_release(struct dentry *dentry)
{
	kfree(dentry->d_fsdata);
}

static const struct dentry_operations ashmem_compat_dentry_ops = {
	.d_dname = ashmem_compat_dname,
	.d_release = ashmem_compat_dentry_release,
};

/*
 * ashmem_compat_create_memfd_file - Creates a new memfd file structure for use.
 * @name: The name of the memfd file.
 * @flags: the MFD_* flags to use when creating the file.
 *
 * The file structure returned by this function must be freed via a
 * call to ashmem_compat_release().
 */
struct file *ashmem_compat_create_memfd_file(const char *name, unsigned int flags)
{
	return memfd_alloc_file(name, flags);
}

/*
 * ashmem_compat_put_memfd_file - Remove a reference to the memfd file structure.
 * @file: The memfd file structure.
 *
 * This function should only be called on file structures allocated via
 * ashmem_compat_open(). It simply drops a reference to the memfd file
 * structure.
 */
int ashmem_compat_put_memfd_file(struct file *file)
{
	if (file && is_ashmem_compat_file(file))
		fput(file);

	return 0;
}

/*
 * ashmem_compat_read_iter - read_iter handler for ashmem compatible memfds.
 * @file: The memfd file structure.
 * @iocb: The IO state structure.
 * @iter: The iterator structure.
 */
ssize_t ashmem_compat_read_iter(struct file *file, struct kiocb *iocb, struct iov_iter *iter)
{
	ssize_t ret;
	struct inode *inode;

	if (!file || !is_ashmem_compat_file(file) || !iocb || !iter)
		return 0;

	if (ashmem_compat_get_size(file) == 0)
		return 0;

	ret = vfs_iter_read(file, iter, &iocb->ki_pos, 0);
	if (ret > 0) {
		inode = file_inode(file);

		inode_lock(inode);
		file->f_pos = iocb->ki_pos;
		inode_unlock(inode);
	}

	return ret;
}

/*
 * ashmem_compat_llseek - llseek handler for ashmem compatible memfds.
 * @file: The memfd file structure.
 * @offset: The offset at which to place the file position.
 * @whence: How to interpret the @offset argument.
 */
loff_t ashmem_compat_llseek(struct file *file, loff_t offset, int origin)
{
	if (!file || !is_ashmem_compat_file(file))
		return -EINVAL;

	if (ashmem_compat_get_size(file) == 0)
		return -EINVAL;

	return vfs_llseek(file, offset, origin);
}

/*
 * ashmem_compat_ioctl - ioctl handler for memfds.
 * @file: The memfd file.
 * @cmd: The ioctl command.
 * @arg: The argument for the ioctl command.
 *
 * The purpose of this handler is to allow old applications to continue working
 * on newer kernels by allowing them to invoke ashmem ioctl commands on memfds.
 *
 * The ioctl handler attempts to retain as much compatibility with the ashmem
 * driver as possible.
 */
long ashmem_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOTTY;
	unsigned long inode_nr;

	if (!file || !is_ashmem_compat_file(file))
		return -EBADF;

	switch (cmd) {
	case ASHMEM_SET_NAME:
		ret = ashmem_compat_set_name(file, (void __user *)arg);
		break;
	case ASHMEM_GET_NAME:
		ret = ashmem_compat_get_name(file, (void __user *)arg);
		break;
	case ASHMEM_SET_SIZE:
		ret = ashmem_compat_set_size(file, (size_t)arg);
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
	/*
	 * Unpinning ashmem buffers was deprecated with the release of Android 10,
	 * as it did not yield any remarkable benefits. Therefore, ignore pinning
	 * related requests.
	 *
	 * This makes it so that memory is always "pinned" or never entirely freed
	 * until all references to the ashmem buffer are dropped. The memory occupied
	 * by the buffer is still subject to being reclaimed (swapped out) under memory
	 * pressure, but that is not the same as being freed.
	 *
	 * This makes it so that:
	 *
	 * 1. Memory is always pinned and therefore never purged.
	 * 2. Requests to unpin memory (make it a candidate for being freed) are ignored.
	 */
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
		ret = capable(CAP_SYS_ADMIN) ? 0 : -EPERM;
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

#ifdef CONFIG_COMPAT
/*
 * ashmem_compat_ioctl_compat - ioctl handler for 32-bit userspace processes.
 * @file: The memfd file.
 * @cmd: The ioctl command.
 * @arg: The argument for the ioctl command.
 */
long ashmem_compat_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (!file || !is_ashmem_compat_file(file))
		return -EBADF;

	if (cmd == COMPAT_ASHMEM_SET_SIZE)
		cmd = ASHMEM_SET_SIZE;
	else if (cmd == COMPAT_ASHMEM_SET_PROT_MASK)
		cmd = ASHMEM_SET_PROT_MASK;

	return ashmem_compat_ioctl(file, cmd, arg);
}
#endif

/*
 * ashmem_compat_mmap - validate memfd state and setup vma as part of mmap.
 * @file: The memfd file structure.
 * @vma: The vma associated with the mapping.
 *
 * This function ensures the following:
 *
 * - The caller has set the size of the memfd file before attempting to map it.
 * - The size of the VMA does not exceed the size of the file.
 * - The correct VM_MAY* bits are cleared to prevent mprotect() from changing the mapping such that
 *   it would violate the buffer's prot mask.
 *
 *   It is the caller's responsibility to ensure that the VMA's vm_file is
 *   set to the memfd file. This is always the case when invoked on a memfd
 *   allocated via the memfd_create() system call, but not when the memfd
 *   file might just be a backing file for another type of file (e.g. the
 *   ashmem driver using memfds as backing files).
 */
int ashmem_compat_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct inode *inode = file_inode(file);
	loff_t size;
	int ret;

	if (!file || !is_ashmem_compat_file(file) || !vma)
		return -EINVAL;

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
	 * Note: the mapping permissions are not handled here because the only mapping
	 * permission that can be removed is PROT_WRITE, and that is handled by
	 * memfd_check_seals_mmap(), which is invoked prior to this function. Therefore, there is
	 * no need to handle mapping permissions here.
	 */
	ret = shmem_mmap(file, vma);
	if (!ret)
		set_buffer_mapped(file);
out:
	inode_unlock(inode);
	return ret;
}

/*
 * ashmem_compat_show_fdinfo - Print memfd file information in the same format as the ashmem driver.
 * @m: The seq_file to print the information into.
 * @file: The memfd file structure.
 */
void ashmem_compat_show_fdinfo(struct seq_file *m, struct file *file)
{
	struct inode *inode;
	char *name;

	if (!m || !file || !is_ashmem_compat_file(file))
		return;

	inode = file_inode(file);

	inode_lock_shared(inode);

	seq_printf(m, "inode:\t%ld\n", inode->i_ino);

	if (file->f_path.dentry->d_fsdata)
		name = file->f_path.dentry->d_fsdata;
	else
		name = (char *)file->f_path.dentry->d_name.name;

	seq_printf(m, "name:\t%s\n", name + strlen("memfd:"));

	seq_printf(m, "size:\t%lld\n", i_size_read(inode));

	inode_unlock_shared(inode);
}

/*
 * install_ashmem_compat_fops - Change the fops for a memfd to ashmem_compat_fops.
 * @file: The memfd file structure.
 */
void install_ashmem_compat_fops(struct file *file)
{
	if (!file || !shmem_file(file))
		return;

	spin_lock(&fops_lock);
	if (!ashmem_compat_fops.mmap) {
		ashmem_compat_fops = *file->f_op;

		ashmem_compat_fops.unlocked_ioctl = ashmem_compat_ioctl;
#ifdef CONFIG_COMPAT
		ashmem_compat_fops.compat_ioctl = ashmem_compat_ioctl_compat;
#endif
		/* Overwrite mmap() to be able to do additional checks that ashmem requires. */
		ashmem_compat_fops.mmap = ashmem_compat_mmap;
		ashmem_compat_fops.show_fdinfo = ashmem_compat_show_fdinfo;
	}
	spin_unlock(&fops_lock);

	file->private_data = NULL;
	file->f_op = &ashmem_compat_fops;
	file->f_path.dentry->d_op = &ashmem_compat_dentry_ops;
}

static struct file_operations ashmem_shim_fops = {
	.open = simple_open,
};

bool is_ashmem_shim_file(struct file *file)
{
	return file->f_op == &ashmem_shim_fops;
}

static struct miscdevice ashmem_shim_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ashmem",
	.fops = &ashmem_shim_fops,
};

static int ashmem_shim_init(void)
{
	int ret = misc_register(&ashmem_shim_misc);

	if (ret < 0)
		pr_err("failed to register ashmem misc device\n");

	return ret;
}
device_initcall(ashmem_shim_init);
