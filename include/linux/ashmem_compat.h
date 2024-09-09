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

void setup_ashmem_compat_ioctl(struct file *file);

#endif /* __LINUX_ASHMEM_COMPAT_H */
