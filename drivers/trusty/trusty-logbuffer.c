// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2021 Google LLC
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rtc.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/syscore_ops.h>
#include <linux/vmalloc.h>
#include "trusty-logbuffer.h"

static int trusty_logbuffer_dev_open(struct inode *inode, struct file *file)
{
	struct trusty_logbuffer *instance;
	struct seq_file *sfile;
	int rc;
	BUG_ON(!file->private_data);
	instance =
		container_of(file->private_data, struct trusty_logbuffer, misc);
	file->private_data = NULL;
	rc = seq_open(file, &instance->cfg.seq_ops);
	if (rc < 0) {
		return rc;
	}
	sfile = file->private_data;
	BUG_ON(!sfile);
	sfile->private = instance;
	return 0;
}

static const struct file_operations logbuffer_dev_operations = {
	.owner = THIS_MODULE,
	.open = trusty_logbuffer_dev_open,
	.read = seq_read,
	.release = seq_release,
};

int trusty_logbuffer_register(struct trusty_logbuffer *instance)
{
	int ret;
	BUG_ON(!instance);
	BUG_ON(!instance->cfg.dev);
	BUG_ON(!instance->cfg.seq_ops.start);
	BUG_ON(!instance->cfg.seq_ops.next);
	BUG_ON(!instance->cfg.seq_ops.show);
	BUG_ON(!instance->cfg.seq_ops.stop);
	snprintf(instance->device_name, sizeof(instance->device_name),
		 "logbuffer_%s%d", instance->cfg.name, instance->cfg.id);
	instance->misc.minor = MISC_DYNAMIC_MINOR;
	instance->misc.name = instance->device_name;
	instance->misc.fops = &logbuffer_dev_operations;

	ret = misc_register(&instance->misc);
	if (ret) {
		dev_err(instance->cfg.dev,
			"Logbuffer error while doing misc_register ret=%d\n",
			ret);
		return ret;
	}
	dev_info(instance->cfg.dev, "/dev/%s registered\n",
		 instance->device_name);
	return 0;
}

void trusty_logbuffer_unregister(struct trusty_logbuffer *instance)
{
	if (!instance)
		return;
	misc_deregister(&instance->misc);
	if (instance->cfg.dev) {
		dev_info(instance->cfg.dev, "/dev/%s unregistered\n",
			 instance->misc.name);
	}
}
