/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_USB_CONFIGFS_UEVENT_H
#define __LINUX_USB_CONFIGFS_UEVENT_H

#include <linux/device.h>
#include <linux/errno.h>

#ifdef CONFIG_USB_CONFIGFS_UEVENT
struct device *create_function_device(char *name);
#else
static inline struct device *create_function_device(char *name)
{
	return ERR_PTR(-ENODEV);
}
#endif /* CONFIG_USB_CONFIGFS_UEVENT */
#endif /* __LINUX_USB_CONFIGFS_UEVENT_H */
