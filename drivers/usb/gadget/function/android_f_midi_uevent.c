// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2011-2024 Google LLC
 */
#include <linux/usb/android_configfs_uevent.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/device.h>

#include "android_f_midi_uevent.h"

int android_set_midi_device_info(struct f_midi_uevent_opts *opts, int card_number,
		unsigned int rmidi_device)
{
	unsigned long flags;

	spin_lock_irqsave(&opts->lock, flags);
	if (opts->configured) {
		spin_unlock_irqrestore(&opts->lock, flags);
		return -EBUSY;
	}
	opts->card_number = card_number;
	opts->rmidi_device = rmidi_device;
	opts->configured = true;
	spin_unlock_irqrestore(&opts->lock, flags);
	return 0;
}
EXPORT_SYMBOL_GPL(android_set_midi_device_info);

static ssize_t f_midi_uevent_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct f_midi_uevent_opts *opts = dev_get_drvdata(dev);
	unsigned long flags;
	int ret;

	/* print PCM card and device numbers or '-1 -1' if unconfigured */
	spin_lock_irqsave(&opts->lock, flags);
	if (opts->configured) {
		ret = sysfs_emit(buf, "%d %d\n",
		opts->card_number, opts->rmidi_device);
	} else {
		// This could occur if the sysfs entry is read prior to binding
		dev_dbg(dev, "f_midi: function not configured\n");
		ret = sysfs_emit(buf, "-1 -1\n");
	}
	spin_unlock_irqrestore(&opts->lock, flags);

	return ret;
}
static DEVICE_ATTR_RO(f_midi_uevent);

static struct attribute *f_midi_uevent_attrs[] = {
	&dev_attr_f_midi_uevent.attr,
	NULL
};
ATTRIBUTE_GROUPS(f_midi_uevent);

int android_create_midi_device(struct f_midi_uevent_opts *opts)
{
	struct device *dev;
	int err;

	spin_lock_init(&opts->lock);
	opts->configured = false;
	dev = android_create_function_device("f_midi");
	if (IS_ERR(dev))
		return PTR_ERR(dev);

	dev_set_drvdata(dev, opts);

	opts->dev = dev;

	err = device_add_groups(dev, f_midi_uevent_groups);
	if (err) {
		dev_err(dev, "Failed to create f_midi sysfs nodes\n");
		device_destroy(dev->class, dev->devt);
		return err;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(android_create_midi_device);

void android_remove_midi_device(struct f_midi_uevent_opts *opts)
{
	unsigned long flags;

	spin_lock_irqsave(&opts->lock, flags);
	if (opts->dev) {
		android_remove_function_device(opts->dev);
		opts->dev = NULL;
	}

	opts->configured = false;
	opts->card_number = 0;
	opts->rmidi_device = 0;
	spin_unlock_irqrestore(&opts->lock, flags);
}
EXPORT_SYMBOL_GPL(android_remove_midi_device);
