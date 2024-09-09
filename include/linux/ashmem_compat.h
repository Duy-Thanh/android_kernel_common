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
#include <linux/memfd.h>
#include <linux/mm_types.h>

void setup_ashmem_compat_ioctl(struct file *file);

extern int do_memfd_create(const char* uname, unsigned int flags, bool ashmem_compat_enable);

extern int shmem_mmap(struct file *file, struct vm_area_struct *vma);

#endif /* __LINUX_ASHMEM_COMPAT_H */
