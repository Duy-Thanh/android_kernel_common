// SPDX-License-Identifier: GPL-2.0
/*
 * Static key and sysfs file controlling VMA readahead boundary.
 *
 * Copyright (c) 2026, Google LLC.
 * Author: Frederick Mayle <fmayle@goole.com>
 */

#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/mm.h>
#include <linux/sysfs.h>
#include <linux/vma_readahead_boundary.h>

DEFINE_STATIC_KEY_FALSE(vma_readahead_boundary_enabled);

static ssize_t show_vma_readahead_boundary_enabled(struct kobject *kobj,
					     struct kobj_attribute *attr,
					     char *buf)
{
	if (is_vma_readahead_boundary_enabled())
		return sprintf(buf, "%d\n", 1);
	else
		return sprintf(buf, "%d\n", 0);
}

static ssize_t store_vma_readahead_boundary_enabled(struct kobject *kobj,
					      struct kobj_attribute *attr,
					      const char *buf, size_t n)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	if (val == 1)
		static_branch_enable(&vma_readahead_boundary_enabled);
	else if (val == 0)
		static_branch_disable(&vma_readahead_boundary_enabled);

	return n;
}

static struct kobj_attribute vma_readahead_boundary_enabled_attr = __ATTR(
	enabled,
	0644,
	show_vma_readahead_boundary_enabled,
	store_vma_readahead_boundary_enabled
);

static struct attribute *vma_readahead_boundary_attrs[] = {
	&vma_readahead_boundary_enabled_attr.attr,
	NULL
};

static struct attribute_group vma_readahead_boundary_attr_group = {
	.name = "vma_readahead_boundary",
	.attrs = vma_readahead_boundary_attrs,
};

/**
 * What:          /sys/kernel/mm/vma_readahead_boundary/enabled
 * Date:          April 2026
 * KernelVersion: v6.12+ (GKI kernels)
 * Contact:       Frederick Mayle <fmayle@google.com>
 * Description:   /sys/kernel/mm/vma_readahead_boundary/enabled
 *		  controls whether readahead triggered by mmap accesses will be
 *		  limited to the bounds of the accessed VMA.
 * Users:         n/a
 */
static int __init init_vma_readahead_boundary(void)
{
	if (sysfs_create_group(mm_kobj, &vma_readahead_boundary_attr_group))
		pr_err("vma_readahead_boundary: failed to create sysfs group\n");

	return 0;
};
late_initcall(init_vma_readahead_boundary);
