// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 */

#include <linux/init.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/bootconfig.h>

#define ANDROID_BOOT_DEV_MAX_V3    30

static char android_boot_dev_v3[ANDROID_BOOT_DEV_MAX_V3];
static const char *android_boot_dev_v4;

const char * get_storage_boot_device(void)
{
	if (android_boot_dev_v4 && strlen(android_boot_dev_v4))
		return android_boot_dev_v4;

	else if (strlen(android_boot_dev_v3))
		return android_boot_dev_v3;

	pr_err("Not able to get Bootconfig or Kernel command line param\n");
	return NULL;
};
EXPORT_SYMBOL(get_storage_boot_device);

static int __init get_android_boot_dev_v3(char *str)
{
	strscpy(android_boot_dev_v3, str, ANDROID_BOOT_DEV_MAX_V3);
	return 1;
}
__setup("androidboot.bootdevice=", get_android_boot_dev_v3);

static int __init get_android_boot_dev_v4(void)
{
	struct xbc_node *vnode = NULL;

	android_boot_dev_v4 = xbc_find_value("androidboot.bootdevice", &vnode);
	if (vnode && xbc_node_is_array(vnode))
		xbc_array_for_each_value(vnode, android_boot_dev_v4)

	pr_info("androidboot bootdevice V4 %s\n", android_boot_dev_v4);
	return 0;
}
fs_initcall(get_android_boot_dev_v4);
