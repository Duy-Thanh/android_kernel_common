/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2024 Google LLC
 */
#ifndef _ANDROID_USB_CONFIGFS_UEVENT_H
#define _ANDROID_USB_CONFIGFS_UEVENT_H

#ifdef CONFIG_ANDROID_USB_CONFIGFS_UEVENT
#include <linux/device.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>

struct android_uevent_opts {
	struct device *dev;
	int device_id;
	bool connected;
	bool configured;
	bool sw_connected;
	struct work_struct work;
	struct ida function_ida;
};
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#else

struct android_uevent_opts {};

||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======

/**
 * android_create_function_device - creates a device within the android_usb
 * class with a new minor number.
 * @name: the name for the device which is to be created
 * @drvdata: the data to be added to the device for callbacks, can be NULL
 * @groups: NULL-terminated list of attribute groups to be created, can be NULL
 *
 * This should be called by function drivers which wish to register a device
 * within the android_usb class.
 *
 * Returns: a pointer to the newly created device upon success, or an ERR_PTR
 * for the encountered error.
 */
struct device *android_create_function_device(char *name, void *drvdata,
		const struct attribute_group **groups);

/**
 * android_remove_function_device - destroys a device which was created by
 * calling android_create_function_device, and performs any necessary cleanup.
 * @dev: the device to be destroyed
 */
void android_remove_function_device(struct device *dev);
#else

struct android_uevent_opts {};

static inline struct device *android_create_function_device(char *name)
{
	return ERR_PTR(-ENODEV);
}

static inline void android_remove_function_device(struct device *dev)
{
}
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
#endif /* CONFIG_ANDROID_USB_CONFIGFS_UEVENT */
#endif /* _ANDROID_USB_CONFIGFS_UEVENT_H */
