// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * experiments-sample.c - Kernel Experiments module
 *
 * Copyright (C) 2025 Deepa Dinamani <deepadinamani@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernfs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/sysfs.h>

struct exp_attr {
	struct attribute attr;
	int enable;
};

static void exp_release(struct kobject *kobj)
{
	kfree(kobj);
}

static ssize_t exp_show(struct kobject *kobj, struct attribute *attr,
			char *buf)
{
	struct exp_attr *exp = container_of(attr, struct exp_attr, attr);
	return sysfs_emit(buf, "%d\n", exp->enable);
}

static ssize_t exp_store(struct kobject *kobj, struct attribute *attr,
			 const char *buf, size_t count)
{
	struct exp_attr *exp = container_of(attr, struct exp_attr, attr);
	int ret, val, orig;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0)
		return ret;

	orig = exp->enable;
	if (val == 0 || val == 1) {
		exp->enable = val;
	} else {
		ret = -EINVAL;
	}

	return ret;
}

#define EXPERIMENT(_name) { .attr = { .name = __stringify(_name), .mode = 0664 }, .enable = 0, }

enum ExperimentTags {
	EXPERIMENT_UNAME_TRACE,
	EXPERIMENT_COUNT
};

static struct exp_attr experiments[] = {
	EXPERIMENT(uname_trace),
};

void trace_android_vh_uname_trace(unsigned int n)
{
	if (!experiments[EXPERIMENT_UNAME_TRACE].enable)
		return;
        printk(KERN_WARNING "kernel example experiment value = %d\n", n);
}

static const struct sysfs_ops exp_ops = {
	.show = exp_show,
	.store = exp_store,
};

static const struct kobj_type exp_ktype = {
	.release = exp_release,
	.sysfs_ops = &exp_ops,
};

static struct kobject *experiments_kobj;

static int experiments_init(void)
{
	int ret, i;

	experiments_kobj = (struct kobject *)kzalloc(sizeof(struct kobject), GFP_KERNEL);
	if (!experiments_kobj)
		return -ENOMEM;

	ret = kobject_init_and_add(experiments_kobj, &exp_ktype, kernel_kobj, "%s", "experiments");
	if (ret) {
		kobject_put(experiments_kobj);
		return ret;
	}

	for(i = 0; i < ARRAY_SIZE(experiments); i++) {
		ret = sysfs_create_file(experiments_kobj, &experiments[i].attr);
		if (ret)
			return ret;

		printk(KERN_WARNING "Initialized android kernel experiment %s\n", experiments[i].attr.name);
	}

	return ret;
}

static void experiments_exit(void)
{
	kobject_put(experiments_kobj);
}

module_init(experiments_init);
module_exit(experiments_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(experiments, "Y");
