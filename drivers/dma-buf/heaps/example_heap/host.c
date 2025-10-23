// SPDX-License-Identifier: GPL-2.0

#include <asm/esr.h>
#include <asm/kvm_pkvm.h>
#include <asm/kvm_pkvm_module.h>
#include <asm/word-at-a-time.h>
#include <kunit/test.h>
#include <linux/dma-heap.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "hyp/example_heap.h"

#ifndef MODULE
BUILD_BUG("Example pKVM heap must be compiled as a module");
#endif

unsigned long protect_page_hvc, unprotect_page_hvc;

static long pkvm_vendor_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct file *f;
	int ret;

	if (ioctl != PKVM_VENDOR_IOCTL_ENABLE_SMC)
		return -ENOTSUPP;
	f = fget(arg);
	if (!f)
		return -EINVAL;

	ret = pkvm_enable_smc_forwarding(f);
	fput(f);

	return ret;
}

static struct file_operations pkvm_vendor_ops = {
	.unlocked_ioctl = pkvm_vendor_ioctl,
};

static struct miscdevice pkvm_vendor_dev = {
	MISC_DYNAMIC_MINOR,
	"pkvm_vendor",
	&pkvm_vendor_ops,
};

static int host_init(void)
{
	struct dma_heap_export_info exp_info;
	struct dma_heap *ex_heap;
	int ret;

	exp_info.name = "example_heap";
	exp_info.ops = &system_heap_modified_ops;
	exp_info.priv = NULL;

	ex_heap = dma_heap_add(&exp_info);
	if (IS_ERR(ex_heap))
		return PTR_ERR(ex_heap);

	ret = pkvm_load_el2_module(__kvm_nvhe_hyp_init);
	if (ret)
		return ret;

	protect_page_hvc = pkvm_register_el2_mod_call(__kvm_nvhe_protect_page);
	if (protect_page_hvc < 0)
		return -EINVAL;

	unprotect_page_hvc = pkvm_register_el2_mod_call(__kvm_nvhe_unprotect_page);
	if (unprotect_page_hvc < 0)
		return -EINVAL;

	return misc_register(&pkvm_vendor_dev);
}
module_init(host_init);

MODULE_DESCRIPTION("Example pKVM dma-buf heap");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_IMPORT_NS("DMA_BUF_HEAP");
