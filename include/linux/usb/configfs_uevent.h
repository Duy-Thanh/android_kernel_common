/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2024 Google LLC
 */
#ifndef _ANDROID_USB_CONFIGFS_UEVENT_H
#define _ANDROID_USB_CONFIGFS_UEVENT_H

#ifdef CONFIG_ANDROID_USB_CONFIGFS_UEVENT
#include <linux/device.h>
#include <linux/workqueue.h>

struct android_uevent_opts {
	struct device *dev;
	int device_index;
	int gadget_index;
	bool connected;
	bool configured;
	bool sw_connected;
	struct work_struct work;
};

struct device *android_create_function_device(char *name);

#else

struct android_uevent_opts {};

static inline struct device *android_create_function_device(char *name)
{
	return ERR_PTR(-ENODEV);
}
#endif /* CONFIG_ANDROID_USB_CONFIGFS_UEVENT */
#endif /* _ANDROID_USB_CONFIGFS_UEVENT_H */
