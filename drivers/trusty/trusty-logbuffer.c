// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */

#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rtc.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/vmalloc.h>
#include "trusty-logbuffer.h"
#define ID_LENGTH 50

struct trusty_logbuffer {
	struct miscdevice misc;
	char id[ID_LENGTH];
	char name[50];
	struct device *dev;
	void *ctx;
	int (*show)(void *ctx, struct seq_file *s, void *v);
};

static int trusty_logbuffer_seq_show(struct seq_file *s, void *v)
{
	struct trusty_logbuffer *instance;
	instance = s->private;
	BUG_ON(!instance->show);
	return (instance->show(instance->ctx, s, v));
}

static int trusty_logbuffer_dev_open(struct inode *inode, struct file *file)
{
	struct trusty_logbuffer *instance =
		container_of(file->private_data, struct trusty_logbuffer, misc);

	inode->i_private = instance;
	file->private_data = NULL;
	return single_open(file, trusty_logbuffer_seq_show, inode->i_private);
}

static const struct file_operations logbuffer_dev_operations = {
	.owner = THIS_MODULE,
	.open = trusty_logbuffer_dev_open,
	.read = seq_read,
	.release = single_release,
};

struct trusty_logbuffer *
trusty_logbuffer_register(const char *name, struct device *dev, void *ctx,
			  int (*show)(void *, struct seq_file *, void *))
{
	struct trusty_logbuffer *instance;
	int ret;
	BUG_ON(!name);
	BUG_ON(!dev);
	BUG_ON(!show);
	instance = kzalloc(sizeof(*instance), GFP_KERNEL);
	if (!instance)
		return ERR_PTR(-ENOMEM);
	instance->show = show;
	instance->dev = dev;
	instance->ctx = ctx;

	strlcpy(instance->name, "logbuffer_", sizeof(instance->name));
	strlcat(instance->name, name, sizeof(instance->name));
	instance->misc.minor = MISC_DYNAMIC_MINOR;
	instance->misc.name = instance->name;
	instance->misc.fops = &logbuffer_dev_operations;

	ret = misc_register(&instance->misc);
	if (ret) {
		dev_err(instance->dev,
			"Logbuffer error while doing misc_register ret=%d\n",
			ret);
		goto free_instance;
	}

	strlcpy(instance->id, name, sizeof(instance->id));

	dev_info(instance->dev, "id:%s registered\n", name);
	return instance;

free_instance:
	kfree(instance);

	return ERR_PTR(-ENOMEM);
}

void trusty_logbuffer_unregister(struct trusty_logbuffer *instance)
{
	if (!instance)
		return;
	misc_deregister(&instance->misc);
	if (instance->dev) {
		dev_info(instance->dev, "id:%s unregistered\n", instance->id);
	}
	kfree(instance);
}
