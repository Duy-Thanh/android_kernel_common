#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/printk.h>
#include <trace/hooks/ext4.h>

// Do we just use global variables everywhere in the kernel?
static bool is_fsync_disabled = false;
static struct kobject *disable_kobj;

static ssize_t disable_fsync_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%d\n", is_fsync_disabled);
}

static ssize_t disable_fsync_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t size)
{
    // Not sure what kstrtobool would leave in the global var if it failed.
    bool read_value;
    if (kstrtobool(buf, &read_value) < 0)
        return -EINVAL;

    is_fsync_disabled = read_value;
    return size;
}

// Use standard kernel kobj_attribute, no custom struct needed
static struct kobj_attribute fsync_disable_attribute =
    __ATTR(disabled, 0664, disable_fsync_show, disable_fsync_store);

static void disable_fsync_handler(void *data, bool *disabled)
{
    *disabled = is_fsync_disabled;
}

int disable_fsync_init(void)
{
    int ret;
    pr_info("init disable_fsync.\n");

    disable_kobj = kobject_create_and_add("disable_ext4_fsync", kernel_kobj);
    if (!disable_kobj)
        return -ENOMEM;

    ret = sysfs_create_file(disable_kobj, &fsync_disable_attribute.attr);
    if (ret) {
        pr_err("Failed to make disable_ext4_fsync sysfs file: %d\n", ret);
        kobject_put(disable_kobj);
        return ret;
    }

    ret = register_trace_android_vh_disable_ext4_fsync(disable_fsync_handler, NULL);
    if (ret) {
        pr_err("Failed to register disable_ext4_fsync hook: %d\n", ret);
        kobject_put(disable_kobj);
        return ret;
    }

    return 0;
}

void disable_fsync_exit(void)
{
    pr_info("cleanup disable_fsync.\n");

    unregister_trace_android_vh_disable_ext4_fsync(disable_fsync_handler, NULL);
    tracepoint_synchronize_unregister();

    if (disable_kobj)
        kobject_put(disable_kobj);
}

module_init(disable_fsync_init);
module_exit(disable_fsync_exit);
