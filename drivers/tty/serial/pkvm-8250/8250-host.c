#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <asm/kvm_pkvm_module.h>

#ifndef MODULE
BUILD_BUG("pKVM 8250 UART must be compiled as a module");
#endif

int kvm_nvhe_sym(hyp_8250_init)(const struct pkvm_module_ops *ops);

static int __init pkvm_8250_init(void)
{
	unsigned long token;
	int ret;

	ret = pkvm_load_el2_module(kvm_nvhe_sym(hyp_8250_init), &token);
	if (ret)
		return ret;

	return 0;
}
module_init(pkvm_8250_init);

MODULE_LICENSE("GPL");
