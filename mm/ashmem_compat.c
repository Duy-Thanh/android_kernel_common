// SPDX-License-Identifier: GPL-2.0

/*
 * Ashmem compatability for memfd
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Carlos Galo <carlosgalo@google.com>
 */

#ifdef CONFIG_MEMFD_ASHMEM_COMPAT

#include <linux/ashmem_compat.h>
#include <linux/hugetlb.h>
#include <linux/ioctl.h>
#include <linux/mm.h>
#include <linux/shmem_fs.h>

struct ashmem_pin {
	__u32 offset;	/* offset into region, in bytes, page-aligned */
	__u32 len;	/* length forward from offset, in bytes, page-aligned */
};

#define __ASHMEMIOC		0x77
#define ASHMEM_NAME_LEN		256
/* Return values from ASHMEM_PIN: Was the mapping purged while unpinned? */
#define ASHMEM_NOT_PURGED       0
#define ASHMEM_WAS_PURGED       1

/* Return values from ASHMEM_GET_PIN_STATUS: Is the mapping pinned? */
#define ASHMEM_IS_UNPINNED      0
#define ASHMEM_IS_PINNED        1

#define ASHMEM_SET_NAME		_IOW(__ASHMEMIOC, 1, char[ASHMEM_NAME_LEN])
#define ASHMEM_GET_NAME		_IOR(__ASHMEMIOC, 2, char[ASHMEM_NAME_LEN])
#define ASHMEM_SET_SIZE32	_IOW(__ASHMEMIOC, 3, u32)
#define ASHMEM_SET_SIZE64	_IOW(__ASHMEMIOC, 3, u64)
#define ASHMEM_GET_SIZE		_IO(__ASHMEMIOC, 4)
#define ASHMEM_SET_PROT_MASK32	_IOW(__ASHMEMIOC, 5, u32)
#define ASHMEM_SET_PROT_MASK64	_IOW(__ASHMEMIOC, 5, u64)
#define ASHMEM_GET_PROT_MASK	_IO(__ASHMEMIOC, 6)
#define ASHMEM_PIN		_IOW(__ASHMEMIOC, 7, struct ashmem_pin)
#define ASHMEM_UNPIN		_IOW(__ASHMEMIOC, 8, struct ashmem_pin)
#define ASHMEM_GET_PIN_STATUS	_IO(__ASHMEMIOC, 9)
#define ASHMEM_PURGE_ALL_CACHES	_IO(__ASHMEMIOC, 10)


static int ashmem_compat_get_name(struct file *file, void __user *name)
{
	int ret = 0;
	/*
	 * Have a local variable to which we'll copy the content
	 * from file with the lock held. Later we can copy this to the user
	 * space safely without holding any locks. So even if we proceed to
	 * wait for mmap_lock, it won't lead to deadlock.
	 */
	char local_name[ASHMEM_NAME_LEN];

	strscpy(local_name, file->f_path.dentry->d_iname, ASHMEM_NAME_LEN);

	if (copy_to_user(name, local_name, ASHMEM_NAME_LEN))
		ret = -EFAULT;
	return ret;
}

static u64 ashmem_compat_get_size(struct file *file)
{
	struct inode *inode = file_inode(file);

	return (u64)i_size_read(inode);
}

static int set_prot_mask(struct file *file, unsigned long prot)
{
	return 0;
}

static unsigned int *memfd_file_seals_ptr(struct file *file)
{
	if (shmem_file(file))
		return &SHMEM_I(file_inode(file))->seals;

#ifdef CONFIG_HUGETLBFS
	if (is_file_hugepages(file))
		return &HUGETLBFS_I(file_inode(file))->seals;
#endif

	return NULL;
}


static long ashmem_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned int *file_seals;
	long ret = -ENOTTY;

	switch (cmd) {
	case ASHMEM_SET_NAME:
		ret = 0;
		break;
	case ASHMEM_GET_NAME:
		ret = ashmem_compat_get_name(file, (void __user *)arg);
		break;
	case ASHMEM_SET_SIZE32:
	case ASHMEM_SET_SIZE64:
		ret = 0;
		break;
	case ASHMEM_GET_SIZE:
		ret = ashmem_compat_get_size(file);
		break;
	case ASHMEM_SET_PROT_MASK32:
	case ASHMEM_SET_PROT_MASK64:
		ret = set_prot_mask(file, arg);
		break;
	case ASHMEM_GET_PROT_MASK:
		file_seals = memfd_file_seals_ptr(file);
		// TODO: Translate seals to PROT_*
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
	}

	return ret;
}

static unsigned long
ashmem_compat_get_unmapped_area(struct file *file, unsigned long addr,
				unsigned long len, unsigned long pgoff,
				unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}


struct file_operations ashmem_compat_fops;

void setup_ashmem_compat_ioctl(struct file *file)
{
	/* Copy the shmem file f_op and setup the unlocked_ioctl */
	if (!ashmem_compat_fops.unlocked_ioctl) {
		ashmem_compat_fops = *file->f_op;
		ashmem_compat_fops.unlocked_ioctl = ashmem_compat_ioctl;
		ashmem_compat_fops.get_unmapped_area =
					ashmem_compat_get_unmapped_area;
	}
	file->f_op = &ashmem_compat_fops;
}


#else /* CONFIG_MEMFD_ASHMEM_COMPAT */
#define setup_ashmem_compat_ioctl(x)
#endif /* CONFIG_MEMFD_ASHMEM_COMPAT */
