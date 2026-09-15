/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (C) 2026 Google LLC
 */

#ifndef _UAPI_LINUX_ANDROID_BPF_FD_LOADER_H
#define _UAPI_LINUX_ANDROID_BPF_FD_LOADER_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define ANDROID_BPF_FD_LOADER_MAGIC 'B'

struct android_bpf_fd_loader_arg {
	__u32		loader_fd;
	__u32		ctx_size;
	__aligned_u64	ctx;
};

#define ANDROID_BPF_FD_LOADER_IOCTL_PING \
	_IO(ANDROID_BPF_FD_LOADER_MAGIC, 1)

#define ANDROID_BPF_FD_LOADER_IOCTL_RECEIVE_FD \
	_IOWR(ANDROID_BPF_FD_LOADER_MAGIC, 2, struct android_bpf_fd_loader_arg)

#endif /* _UAPI_LINUX_ANDROID_BPF_FD_LOADER_H */
