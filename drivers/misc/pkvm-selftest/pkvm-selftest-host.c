#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/kvm_pkvm_module.h>

#define HYP_EVENT_FILE ../../../../drivers/misc/pkvm-selftest/hyp/events.h
#include <asm/kvm_define_hypevents.h>

int __kvm_nvhe_pkvm_selftest_init(const struct pkvm_module_ops *ops);
void __kvm_nvhe_pkvm_selftest_event(struct kvm_cpu_context *ctx);

static int hvc_event;

static ssize_t event_write(struct file *f, const char __user *buf,
			       size_t size, loff_t *pos)
{
	int ret = pkvm_el2_mod_call(hvc_event);

	if (ret)
		pr_warn("Failed to call HVC %d: %d\n", hvc_event, ret);

	return size;
}

static const struct file_operations event_fops = {
	.read = NULL,
	.write = event_write,
	.llseek = default_llseek,
};

static int __init pkvm_selftest_init(void)
{
	unsigned long token;
	int ret;

	ret = pkvm_load_el2_module(__kvm_nvhe_pkvm_selftest_init, &token);
	if (ret) {
		pr_warn("Failed to load pKVM module: %d\n", ret);
		return ret;
	}

	ret = pkvm_register_el2_mod_call(__kvm_nvhe_pkvm_selftest_event, token);
	if (ret < 0) {
		pr_warn("Failed to register HVC: %d\n", ret);
		return ret;
	}

	hvc_event = ret;

	debugfs_create_file("pkvm_selftest_event", 0200, NULL, NULL,
			    &event_fops);

	return 0;
}
module_init(pkvm_selftest_init);

MODULE_LICENSE("GPL");
