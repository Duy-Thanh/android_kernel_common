// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2011-2024 Google LLC
 */
#include "configfs_uevent.h"
#include <linux/device.h>
#include <linux/device/class.h>
#include <linux/err.h>
#include <linux/kdev_t.h>
#include <linux/spinlock.h>

static struct android_uevent_opts *g_android_opts;

static DEFINE_SPINLOCK(g_opts_lock);

static void android_work(struct work_struct *data)
{
	struct android_uevent_opts *opts = container_of(data,
			struct android_uevent_opts, work);

	char *disconnected_strs[2] = { "USB_STATE=DISCONNECTED", NULL };
	char *connected_strs[2] = { "USB_STATE=CONNECTED", NULL };
	char *configured_strs[2] = { "USB_STATE=CONFIGURED", NULL };
	unsigned long flags;
	bool disconnected = false;
	bool connected = false;
	bool configured = false;
	bool uevent_sent = false;

	/*
	 * I believe locking is important due to the fact that we are checking
	 * several conditions here, and if the state changes after checking one
	 * we could potentially drop a uevent to userspace.
	 */
	spin_lock_irqsave(&g_opts_lock, flags);

	if (opts->connected != opts->sw_connected) {
		if (opts->connected)
			connected = true;
		else
			disconnected = true;
		opts->sw_connected = opts->connected;
	}
	if (opts->configured)
		configured = true;

	spin_unlock_irqrestore(&g_opts_lock, flags);

	if (connected) {
		kobject_uevent_env(&opts->dev->kobj, KOBJ_CHANGE, connected_strs);
		dev_dbg(opts->dev, "%s: sent uevent %s\n", __func__, connected_strs[0]);
		uevent_sent = true;
	}

	if (opts->configured) {
		kobject_uevent_env(&opts->dev->kobj, KOBJ_CHANGE, configured_strs);
		dev_dbg(opts->dev, "%s: sent uevent %s\n", __func__, configured_strs[0]);
		uevent_sent = true;
	}

	if (disconnected) {
		kobject_uevent_env(&opts->dev->kobj, KOBJ_CHANGE, disconnected_strs);
		dev_dbg(opts->dev, "%s: sent uevent %s\n", __func__, disconnected_strs[0]);
		uevent_sent = true;
	}

	if (!uevent_sent) {
		/*
		 * This is an odd case, but not necessarily an error- the state
		 * of the device may have changed since the work was scheduled,
		 * and if the state changed, there is likely another scheduled
		 *  work which will send a uevent.
		 */
		dev_dbg(opts->dev, "%s: did not send uevent\n", __func__);
	}
}

static ssize_t state_show(struct device *pdev, struct device_attribute *attr,
		char *buf)
{
	struct android_uevent_opts *opts = dev_get_drvdata(pdev);
	char *state = "DISCONNECTED";

	if (opts->configured)
		state = "CONFIGURED";
	else if (opts->connected)
		state = "CONNECTED";

	return sysfs_emit(buf, "%s\n", state);
}
static DEVICE_ATTR_RO(state);

static struct attribute *android_usb_attrs[] = {
	&dev_attr_state.attr,
	NULL,
};

ATTRIBUTE_GROUPS(android_usb);

static struct class android_usb_class = {
	.name = "android_usb",
	.dev_groups = android_usb_groups,
};

int android_class_create(void)
{
	return class_register(&android_usb_class);
}
EXPORT_SYMBOL_GPL(android_class_create);

void android_class_destroy(void)
{
	class_unregister(&android_usb_class);
}
EXPORT_SYMBOL_GPL(android_class_destroy);

int android_device_create(struct android_uevent_opts *opts)
{
	unsigned long flags;

	INIT_WORK(&opts->work, android_work);
	opts->dev = device_create(&android_usb_class, NULL, MKDEV(0, 0),
			       opts, "android%d", opts->gadget_index++);
	if (IS_ERR(opts->dev))
		return PTR_ERR(opts->dev);

	spin_lock_irqsave(&g_opts_lock, flags);
	if (!g_android_opts)
		g_android_opts = opts;
	spin_unlock_irqrestore(&g_opts_lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(android_device_create);

void android_device_destroy(struct android_uevent_opts *opts)
{
	unsigned long flags;

	spin_lock_irqsave(&g_opts_lock, flags);
	g_android_opts = NULL;
	spin_unlock_irqrestore(&g_opts_lock, flags);

	device_destroy(opts->dev->class, opts->dev->devt);
}
EXPORT_SYMBOL_GPL(android_device_destroy);

void __android_set_connected(struct android_uevent_opts *opts, bool connected)
{
	unsigned long flags;

	spin_lock_irqsave(&g_opts_lock, flags);
	// Don't send the uevent if connected state is not changed
	if (opts->connected != connected) {
		opts->connected = connected;
		schedule_work(&opts->work);
	}
	spin_unlock_irqrestore(&g_opts_lock, flags);
}

void __android_set_configured(struct android_uevent_opts *opts, bool configured)
{
	unsigned long flags;

	spin_lock_irqsave(&g_opts_lock, flags);
	// Don't send the uevent if configure state is not changed
	if (opts->configured != configured) {
		opts->configured = configured;
		schedule_work(&opts->work);
	}
	spin_unlock_irqrestore(&g_opts_lock, flags);
}

void android_set_connected(struct android_uevent_opts *opts)
{
	__android_set_connected(opts, true);
}
EXPORT_SYMBOL_GPL(android_set_connected);

void android_set_disconnected(struct android_uevent_opts *opts)
{
	__android_set_connected(opts, false);
}
EXPORT_SYMBOL_GPL(android_set_disconnected);

void android_set_configured(struct android_uevent_opts *opts)
{
	__android_set_configured(opts, true);
}
EXPORT_SYMBOL_GPL(android_set_configured);

void android_set_unconfigured(struct android_uevent_opts *opts)
{
	__android_set_configured(opts, false);
}
EXPORT_SYMBOL_GPL(android_set_unconfigured);

struct device *android_create_function_device(char *name)
{
	struct android_uevent_opts *opts;
	struct device *dev;
	unsigned long flags;

	spin_lock_irqsave(&g_opts_lock, flags);
	opts = g_android_opts;
	if (IS_ERR_OR_NULL(opts) || IS_ERR_OR_NULL(opts->dev))
		return ERR_PTR(-ENODEV);
	dev = device_create(&android_usb_class, opts->dev,
	       MKDEV(0, opts->device_index++), NULL, name);
	spin_unlock_irqrestore(&g_opts_lock, flags);
	return dev;
}
EXPORT_SYMBOL_GPL(android_create_function_device);
