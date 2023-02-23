#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/oom.h>
#include <linux/tracepoint.h>
#include <trace/events/oom.h>

static DEFINE_SPINLOCK(atest_lock);
static uint32_t atest_level;
static uint32_t atest_threshold;
static int level_cross_count;
static int oom_count;

static ssize_t level_cross_count_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sysfs_emit(buf, "%d\n", level_cross_count);
}
static DEVICE_ATTR_RO(level_cross_count);

static ssize_t oom_count_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return sysfs_emit(buf, "%d\n", oom_count);
}
static DEVICE_ATTR_RO(oom_count);

static const struct attribute *atest_attributes[] = {
	&dev_attr_level_cross_count.attr,
	&dev_attr_oom_count.attr,
	NULL
};

static void param_set_attr(uint32_t level, uint32_t threshold)
{
	bool trigger;

	spin_lock(&atest_lock);
	atest_level = level;
	atest_threshold = threshold;
	trigger = level > threshold;
	if (trigger)
		level_cross_count++;
	spin_unlock(&atest_lock);

	if (trigger)
		sysfs_notify(&THIS_MODULE->mkobj.kobj, NULL, dev_attr_level_cross_count.attr.name);
}

static int param_set_level(const char *s, const struct kernel_param *kp)
{
	uint32_t val;
	int ret;

	if (!s)
		return 0;

	printk("New level received: %s\n", s);

	ret = kstrtouint(s, 10, &val);
	if (ret)
		return ret;

	if (val == atest_level)
		return 0;

	printk("New level %u accepted\n", val);

	param_set_attr(val, atest_threshold);

	return 0;
}

static const struct kernel_param_ops param_ops_level = {
	.set = param_set_level,
	.get = param_get_uint,
};

module_param_cb(level, &param_ops_level, &atest_level, 0644);

static int param_set_threshold(const char *s, const struct kernel_param *kp)
{
	uint32_t val;
	int ret;

	if (!s)
		return 0;

	printk("New threshold received: %s\n", s);

	ret = kstrtouint(s, 10, &val);
	if (ret)
		return ret;

	if (val == atest_threshold)
		return 0;

	printk("New threshold %u accepted\n", val);

	param_set_attr(atest_level, val);

	return 0;
}

static const struct kernel_param_ops param_ops_threshold = {
	.set = param_set_threshold,
	.get = param_get_uint,
};

module_param_cb(threshold, &param_ops_threshold, &atest_threshold, 0644);

static struct miscdevice atest_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "atestmod",
};

static void mark_victim_probe(void *data, pid_t pid)
{
	printk("OOM-killer killed process %d\n", pid);
	spin_lock(&atest_lock);
	oom_count++;
	spin_unlock(&atest_lock);
	sysfs_notify(&THIS_MODULE->mkobj.kobj, NULL, dev_attr_oom_count.attr.name);
}

static int __init atestmod_start(void)
{
	int result;

	printk(KERN_INFO "Loading Android test module...\n");
	result = misc_register(&atest_misc);
	if (result < 0)
		return -ENODEV;

	result = sysfs_create_files(&THIS_MODULE->mkobj.kobj, atest_attributes);
	if (result) {
		printk(KERN_ERR "Failed to create sysfs files\n");
		return result;
	}

	if (!register_trace_mark_victim(mark_victim_probe, NULL))
		printk(KERN_INFO "Hooked a probe to the mark_victim tracepoint\n");
	else
		printk(KERN_WARNING "Failed to hook a probe to the mark_victim tracepoint\n");

	printk(KERN_INFO "Android test module loaded\n");

	return 0;
}

static void __exit atestmod_end(void)
{
	printk(KERN_INFO "Unloading Android test module\n");

	if (unregister_trace_mark_victim(mark_victim_probe, NULL))
		printk(KERN_WARNING "Failed to unhook a probe from the mark_victim tracepoint\n");

	sysfs_remove_files(&THIS_MODULE->mkobj.kobj, atest_attributes);

	misc_deregister(&atest_misc);
}

module_init(atestmod_start);
module_exit(atestmod_end);

MODULE_LICENSE("GPL");