// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * exp_attr.c - Kernel Experiments module
 *
 * Copyright (C) 2025 Deepa Dinamani <deepadinamani@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/printk.h>

#include "exp_attr.h"

#include <trace/hooks/experiments.h>

static void android_vh_uname_trace(struct exp_attr *exp, unsigned int n)
{
	if (exp && exp->enable) {
		printk(KERN_INFO pr_fmt("kernel uname_trace hook(%d)\n"), n);
	}
}

struct exp_attr experiments[] = {
	EXPERIMENT(uname_trace),
};

const size_t experiment_count = ARRAY_SIZE(experiments);
