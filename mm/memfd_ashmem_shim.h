/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_MEMFD_ASHMEM_SHIM_H
#define __MM_MEMFD_ASHMEM_SHIM_H

/*
 * mm/memfd_ashmem_shim.h
 *
 * Ashmem compatability for memfd
 *
 * Copyright (c) 2025, Google LLC.
 * Author: Isaac J. Manjarres <isaacmanjarres@google.com>
 *
 */

#include <linux/fs.h>
#include <linux/seq_file.h>

#ifdef CONFIG_MEMFD_ASHMEM_SHIM
long memfd_ashmem_shim_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
long memfd_ashmem_shim_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#endif
#ifdef CONFIG_PROC_FS
void memfd_ashmem_shim_show_fdinfo(struct seq_file *m, struct file *file);
#endif
#else
static inline long memfd_ashmem_shim_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return -ENOTTY;
}
#ifdef CONFIG_COMPAT
static inline long memfd_ashmem_shim_compat_ioctl(struct file *file, unsigned int cmd,
						  unsigned long arg)
{
	return -ENOTTY;
}
#endif
#ifdef CONFIG_PROC_FS
static inline void memfd_ashmem_shim_show_fdinfo(struct seq_file *m, struct file *file)
{
}
#endif
#endif
#endif /* __MM_MEMFD_ASHMEM_SHIM_H */
