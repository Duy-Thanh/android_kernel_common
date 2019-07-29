/*
 * Suspend Statistics in sysfs
 */

#include <linux/suspend.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>

#define suspend_attr(_name)					\
static ssize_t _name##_show(struct kobject *kobj,		\
		struct kobj_attribute *attr, char *buf)		\
{								\
	return sprintf(buf, "%d\n", suspend_stats._name);	\
}								\
static struct kobj_attribute _name = __ATTR_RO(_name);


suspend_attr(success);
suspend_attr(fail);
suspend_attr(failed_freeze);
suspend_attr(failed_prepare);
suspend_attr(failed_suspend);
suspend_attr(failed_suspend_late);
suspend_attr(failed_suspend_noirq);
suspend_attr(failed_resume);
suspend_attr(failed_resume_early);
suspend_attr(failed_resume_noirq);
suspend_attr(last_failed_dev);
suspend_attr(last_failed_errno);
suspend_attr(last_failed_step);

static struct attribute *attrs[] = {
	&success.attr,
	&fail.attr,
	&failed_freeze.attr,
	&failed_prepare.attr,
	&failed_suspend.attr,
	&failed_suspend_late.attr,
	&failed_suspend_noirq.attr,
	&failed_resume.attr,
	&failed_resume_early.attr,
	&failed_resume_noirq.attr,
	&last_failed_dev.attr,
	&last_failed_errno.attr,
	&last_failed_step.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = attrs,
};

static struct kobject *suspend_stats_kobj;

int __init suspend_stats_init(void)
{
	int retval;

	suspend_stats_kobj = kobject_create_and_add("suspend_stats", power_kobj);
	if (!suspend_stats_kobj) {
		printk(KERN_WARNING "[%s] failed to create a sysfs kobject\n",
				__func__);
		return 1;
	}

	retval = sysfs_create_group(suspend_stats_kobj, &attr_group);
	if (retval) {
		kobject_put(suspend_stats_kobj);
		printk(KERN_WARNING "[%s] failed to create a sysfs group %d\n",
				__func__, retval);
	}

	return 0;
}

late_initcall(suspend_stats_init);
