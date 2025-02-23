// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * experiments-sample.c - Kernel Experiments module
 *
 * Copyright (C) 2025 Deepa Dinamani <deepadinamani@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/sysfs.h>

struct exp_kobj {
	struct kobject kobj;
	char *name;
	int enable;
};

static ssize_t exp_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	struct exp_kobj *exp = container_of(kobj, struct exp_kobj, kobj);

	return sysfs_emit(buf, "%d\n", exp->enable);
}

static ssize_t exp_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	struct exp_kobj *exp = container_of(kobj, struct exp_kobj, kobj);
	int ret, val;

	ret = kstrtoint(buf, 10, &val);
	if (ret < 0)
		return ret;

	if (val == 1)
		exp->enable = val;
	else
		ret = -EINVAL;

	if (val)
		ret = kobject_uevent(kobj, KOBJ_CHANGE);

	return ret;
}

static struct kobj_attribute exp_root_attr = __ATTR(, 0664, exp_show, exp_store);

static struct kobject *exp_root_kobj;

static struct exp_kobj experiments[] = {
	{.name = "example_exp", },
};

static int experiments_init(void)
{
	int ret, i;

	exp_root_kobj = kobject_create_and_add("experiments", NULL);
	if (!exp_root_kobj)
		return -ENOMEM;

	ret = sysfs_create_file(exp_root_kobj, &(exp_root_attr.attr));
	if (ret)
		return ret;

	for(i = 0; i < ARRAY_SIZE(experiments); i++) {
		ret = kobject_add(&(experiments[i].kobj),
							exp_root_kobj,
							"%s", experiments[i].name);
		if (ret)
			kobject_put(&(experiments[i].kobj));

	}

	return ret;
}

static void experiments_exit(void)
{
}

module_init(experiments_init);
module_exit(experiments_exit);
MODULE_LICENSE("GPL");
MODULE_INFO(experiments, "Y");
