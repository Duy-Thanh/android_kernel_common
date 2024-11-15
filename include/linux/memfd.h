/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MEMFD_H
#define __LINUX_MEMFD_H

#include <linux/file.h>

#ifdef CONFIG_MEMFD_CREATE
extern long memfd_fcntl(struct file *file, unsigned int cmd, unsigned int arg);
struct folio *memfd_alloc_folio(struct file *memfd, pgoff_t idx);
unsigned int *memfd_file_seals_ptr(struct file *file);
struct file *memfd_filp_create(const char *name, unsigned int flags, bool ashmem_compatible);
#else
static inline long memfd_fcntl(struct file *f, unsigned int c, unsigned int a)
{
	return -EINVAL;
}
static inline struct folio *memfd_alloc_folio(struct file *memfd, pgoff_t idx)
{
	return ERR_PTR(-EINVAL);
}
static inline unsigned int memfd_file_seals_ptr(struct file *file)
{
	return NULL;
}
static inline struct file *memfd_filp_create(const char *name, unsigned int flags,
					     bool ashmem_compatible)
{
	return ERR_PTR(-EINVAL);
}
#endif

#endif /* __LINUX_MEMFD_H */
