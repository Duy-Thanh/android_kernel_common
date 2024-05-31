// SPDX-License-Identifier: GPL-2.0
/*
 * Swap Cluster Reservation
 *
 * This file contains the knobs to:
 *   - set the percent of swap clusters reserved for large folios
 *   - set the order of folios for which clusters are reserved.
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 */

#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/mm.h>
#include <linux/sysfs.h>

unsigned int vm_swap_cluster_reserve_percent = 0;
unsigned int vm_swap_cluster_reserve_order = 0;

static ssize_t percent_show(struct kobject *kobj, struct kobj_attribute *attr,
			    char *buf)
{
	return sysfs_emit(buf, "%u\n", vm_swap_cluster_reserve_percent);
}

static ssize_t percent_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t n)
{
	unsigned long val;

	/* For simplicity only allow the reservation percent to be set once (before swapon) */
	if (vm_swap_cluster_reserve_percent != 0)
		return -ENOTSUPP;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	/* Only allow reserving form 1 to 99 percent of the clusters */
	if (val < 1 || val > 99)
		return -EINVAL;

	vm_swap_cluster_reserve_percent = (unsigned int)val;

	return n;
}

static struct kobj_attribute swap_cluster_reserve_percent_attr = __ATTR_RW(percent);

static ssize_t order_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	return sysfs_emit(buf, "%u\n", vm_swap_cluster_reserve_order);
}

static ssize_t order_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t n)
{
	unsigned long val;

	/* For simplicity only allow the reservation order to be set once (before swapon) */
	if (vm_swap_cluster_reserve_order != 0)
		return -ENOTSUPP;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	/* Only allow orders greater than the page size but less than pmd size */
	if (val < PAGE_SHIFT || val >= PMD_SHIFT)
		return -EINVAL;

	vm_swap_cluster_reserve_order = (unsigned int)val;

	return n;
}

static struct kobj_attribute swap_cluster_reserve_order_attr = __ATTR_RW(order);

static struct attribute *swap_cluster_reserve_attrs[] = {
	&swap_cluster_reserve_percent_attr.attr,
	&swap_cluster_reserve_order_attr.attr,
	NULL
};

static struct attribute_group swap_cluster_reserve_attr_group = {
	.name = "swap_cluster_reserve",
	.attrs = swap_cluster_reserve_attrs,
};

static int __init init_swap_cluster_reserve(void)
{
	if (sysfs_create_group(mm_kobj, &swap_cluster_reserve_attr_group))
		pr_err("swap_cluster_reserve: failed to create sysfs group\n");

	return 0;
};
late_initcall(init_swap_cluster_reserve);
