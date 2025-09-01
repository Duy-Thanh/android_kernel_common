/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_MEMFD_H
#define __LINUX_MEMFD_H

#include <linux/file.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#define MEMFD_ANON_NAME "[memfd]"

||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
#ifdef CONFIG_MEMFD_CREATE
extern long memfd_fcntl(struct file *file, unsigned int cmd, unsigned int arg);
struct folio *memfd_alloc_folio(struct file *memfd, pgoff_t idx);
unsigned int *memfd_file_seals_ptr(struct file *file);
#else
static inline long memfd_fcntl(struct file *f, unsigned int c, unsigned int a)
{
	return -EINVAL;
}
static inline struct folio *memfd_alloc_folio(struct file *memfd, pgoff_t idx)
{
	return ERR_PTR(-EINVAL);
}

static inline unsigned int *memfd_file_seals_ptr(struct file *file)
{
	return NULL;
}
#endif

/* Retrieve memfd seals associated with the file, if any. */
static inline unsigned int memfd_file_seals(struct file *file)
{
	unsigned int *sealsp = memfd_file_seals_ptr(file);

	return sealsp ? *sealsp : 0;
}

#endif /* __LINUX_MEMFD_H */
