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
#include <linux/of.h>
#include <linux/of_address.h>

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

static int ex_heap_init_config_from_dt(void)
{
	struct device_node *np;
	struct resource res;
	u32 tokens[MAX_CONFIGURED_VMS * 2];
	int count, i, ret = 0;

	np = of_find_compatible_node(NULL, NULL, "pkvmvendor,auth-token");
	if (!np)
		return 0;

	if (of_address_to_resource(np, 0, &res)) {
		pr_err("example_heap: failed to get address for %pOF\n", np);
		ret = -EINVAL;
		goto out_put;
	}

	count = of_property_read_variable_u32_array(np, "tokens", tokens, 2,
						    MAX_CONFIGURED_VMS * 2);
	if (count < 0) {
		pr_err("example_heap: failed to read 'tokens' in %pOF (ret=%d)\n",
		       np, count);
		ret = count;
		goto out_put;
	}

	if (count % 2 != 0) {
		pr_err("example_heap: 'tokens' property must be pairs in %pOF\n",
		       np);
		ret = -EINVAL;
		goto out_put;
	}

	for (i = 0; i < count / 2; i++) {
		u32 token_offset = tokens[i * 2];
		u32 protection_id = tokens[i * 2 + 1];

		if (token_offset > resource_size(&res) ||
		    AUTH_TOKEN_SIZE > resource_size(&res) - token_offset) {
			pr_err("example_heap: token out of bounds for %pOF\n",
			       np);
			ret = -EINVAL;
			goto out_put;
		}

		kvm_nvhe_sym(ex_heap_config).entries[i].token_paddr =
			res.start + token_offset;
		kvm_nvhe_sym(ex_heap_config).entries[i].protection_id =
			protection_id;
	}

	kvm_nvhe_sym(ex_heap_config).num_entries = i;

out_put:
	of_node_put(np);
	return ret;
}

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

	ret = ex_heap_init_config_from_dt();
	if (ret)
		return ret;

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
