// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 - Google Inc
 * Author: Sebastian Ene <sebastianene@google.com>
 * Simple module for pKVM guest SMC proxying.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <asm/kvm_pkvm_module.h>

#define HYP_EVENT_FILE ../../../../drivers/misc/pkvm-smc/pkvm/events.h
#include <asm/kvm_define_hypevents.h>

static unsigned long pkvm_module_token;
int kvm_nvhe_sym(pkvm_guest_smc_proxy_hyp_init)(const struct pkvm_module_ops *ops);

static int __init guest_smc_proxy_init(void)
{
	int ret = 0;

	ret = pkvm_load_el2_module(kvm_nvhe_sym(pkvm_guest_smc_proxy_hyp_init),
				   &pkvm_module_token);
	if (ret)
		pr_err("Failed to register pKVM guest SMC proxy: %d\n", ret);
	else
		pr_info("pKVM guest SMC proxy registered successfully with permissive\n");

	return ret;
}

module_init(guest_smc_proxy_init);

MODULE_AUTHOR("Sebastian Ene <sebastianene@google.com>");
MODULE_DESCRIPTION("pKVM Guest SMC proxy");
MODULE_LICENSE("GPL v2");
