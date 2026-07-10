/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 */

#ifndef VFIO_PLATFORM_PRIVATE_H
#define VFIO_PLATFORM_PRIVATE_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/vfio_platform.h>
#include <linux/vfio.h>

#define VFIO_PLATFORM_OFFSET_SHIFT   40
#define VFIO_PLATFORM_OFFSET_MASK (((u64)(1) << VFIO_PLATFORM_OFFSET_SHIFT) - 1)

#define VFIO_PLATFORM_OFFSET_TO_INDEX(off)	\
	(off >> VFIO_PLATFORM_OFFSET_SHIFT)

#define VFIO_PLATFORM_INDEX_TO_OFFSET(index)	\
	((u64)(index) << VFIO_PLATFORM_OFFSET_SHIFT)

int vfio_platform_init_common(struct vfio_platform_device *vdev);
void vfio_platform_release_common(struct vfio_platform_device *vdev);

int vfio_platform_open_device(struct vfio_device *core_vdev);
void vfio_platform_close_device(struct vfio_device *core_vdev);
long vfio_platform_ioctl(struct vfio_device *core_vdev,
			 unsigned int cmd, unsigned long arg);
int vfio_platform_ioctl_feature(struct vfio_device *core_vdev, u32 flags, void __user *arg,
				size_t argsz);
ssize_t vfio_platform_read(struct vfio_device *core_vdev,
			   char __user *buf, size_t count,
			   loff_t *ppos);
ssize_t vfio_platform_write(struct vfio_device *core_vdev,
			    const char __user *buf,
			    size_t count, loff_t *ppos);
int vfio_platform_mmap(struct vfio_device *core_vdev,
		       struct vm_area_struct *vma);

int vfio_platform_irq_init(struct vfio_platform_device *vdev);
void vfio_platform_irq_cleanup(struct vfio_platform_device *vdev);

int vfio_platform_set_irqs_ioctl(struct vfio_platform_device *vdev,
				 uint32_t flags, unsigned index,
				 unsigned start, unsigned count, void *data);

#endif /* VFIO_PLATFORM_PRIVATE_H */
