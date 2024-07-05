#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>

static int hvc_number;

int __kvm_nvhe_pkvm_driver_hyp_init(const struct pkvm_module_ops *ops);
void __kvm_nvhe_pkvm_break_blocks(struct kvm_cpu_context *ctx);

static int stage2_dump_open(struct inode *inode, struct file *f)
{
	return pkvm_el2_mod_call(hvc_number);
}

static const struct file_operations break_blocks_fops = {
	.open = stage2_dump_open,
	.read = NULL,
	.write = NULL,
	.llseek = default_llseek,
};

static int __init pkvm_driver_init(void)
{
	unsigned long token;
	int ret;

	ret = pkvm_load_el2_module(__kvm_nvhe_pkvm_driver_hyp_init, &token);
	if (ret)
		return ret;

	ret = pkvm_register_el2_mod_call(__kvm_nvhe_pkvm_break_blocks, token);
	if (ret < 0)
		return ret;

	hvc_number = ret;

	debugfs_create_file("break_blocks", 0200, NULL, NULL,
			    &break_blocks_fops);

	return 0;

}
module_init(pkvm_driver_init);

MODULE_LICENSE("GPL");
