/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ASHMEM_COMPAT_H
#define __LINUX_ASHMEM_COMPAT_H

/*
 * include/linux/ashmem_compat.h
 *
 * Ashmem compatability for memfd in Android
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Carlos Galo <carlosgalo@google.com>
 *
 */

#include <linux/file.h>
#include <linux/fs.h>
#include <linux/mm_types.h>
#include <linux/seq_file.h>
#include <linux/uio.h>

#ifdef CONFIG_MEMFD_ASHMEM_COMPAT
struct file *ashmem_compat_create_memfd_file(const char *name, unsigned int flags);
int ashmem_compat_put_memfd_file(struct file *file);
ssize_t ashmem_compat_read_iter(struct file *file, struct kiocb *iocb, struct iov_iter *iter);
loff_t ashmem_compat_llseek(struct file *file, loff_t offset, int origin);
long ashmem_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
long ashmem_compat_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg);
int ashmem_compat_mmap(struct file *file, struct vm_area_struct *vma);
void ashmem_compat_show_fdinfo(struct seq_file *m, struct file *file);
void install_ashmem_compat_fops(struct file *file);
bool is_ashmem_shim_file(struct file *file);
#else
static inline struct file *ashmem_compat_create_memfd_file(const char *name, unsigned int flags)
{
	return ERR_PTR(-ENODEV);
}
static inline int ashmem_compat_put_memfd_file(struct file *file)
{
	return 0;
}
static inline ssize_t ashmem_compat_read_iter(struct file *file, struct kiocb *iocb,
					      struct iov_iter *iter)
{
	return -EINVAL;
}
static inline loff_t ashmem_compat_llseek(struct file *file, loff_t offset, int origin)
{
	return -EINVAL;
}
static inline long ashmem_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}
static inline long ashmem_compat_ioctl_compat(struct file *file, unsigned int cmd,
					      unsigned long arg)
{
	return -ENOTTY;
}
static inline int ashmem_compat_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -ENODEV;
}
static inline void ashmem_compat_show_fdinfo(struct seq_file *m, struct file *file)
{
}
static inline void install_ashmem_compat_fops(struct file *file)
{
}
static inline bool is_ashmem_shim_file(struct file *file)
{
	return false;
}
#endif
#endif /* __LINUX_ASHMEM_COMPAT_H */
