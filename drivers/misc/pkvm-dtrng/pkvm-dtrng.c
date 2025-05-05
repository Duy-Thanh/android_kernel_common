// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>

int kvm_nvhe_sym(pkvm_dtrng_init)(const struct pkvm_module_ops *ops);

void kvm_nvhe_sym(pkvm_dtrng_feed_hvc)(struct user_pt_regs *regs);

static int hvc_feed_no;

static int __init dtrng_init(void)
{
	int ret;
	unsigned long token;
	void *buf;

	ret = pkvm_load_el2_module(kvm_nvhe_sym(pkvm_dtrng_init), &token);
	if (ret) {
		pr_err("Failed to register pKVM module: %d\n", ret);
		return ret;
	}

	ret = pkvm_register_el2_mod_call(kvm_nvhe_sym(pkvm_dtrng_feed_hvc),
					 token);
	if (ret < 0) {
		pr_err("Failed to register hyp call: %d\n", ret);
		return ret;
	}

	hvc_feed_no = ret;

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (buf == NULL) {
		pr_err("Failed to allocate memory: %d\n", ret);
		return -ENOMEM;
	}

	memset(buf, 0x42, PAGE_SIZE);

	ret = pkvm_el2_mod_call(hvc_feed_no, virt_to_phys(buf), PAGE_SIZE);
	if (ret) {
		pr_err("Failed to feed: %d\n", ret);
		return ret;
	}

	pr_err("hvc_feed_no = %d\n", hvc_feed_no);

	return 0;
}

module_init(dtrng_init);

MODULE_AUTHOR("Bartlomiej Grzesik <bgrzesik@google.com>");
MODULE_DESCRIPTION("DTRNG");
MODULE_LICENSE("GPL v2");
