// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "../../../tools/testing/selftests/kselftest_module.h"

#include <linux/slab.h>

#include <asm/kvm_s2mpu.h>

KSTM_MODULE_GLOBALS();

#define ASSERT(cond)							\
	do {								\
		if (!(cond)) {						\
			pr_err("line %d: assertion failed: %s\n",	\
			       __LINE__, #cond);			\
			return -1;					\
		}							\
	} while (0)

static struct fmpt g_fmpt;
/* Reserve memory for bigger SMPT. */
static u32 g_smpt[SMPT_NUM_WORDS(V9_MPT_PROT_BITS)];

#define ARG_PROT_BITS	MPT_PROT_BITS
#define ARG_ACCESS_SHIFT MPT_ACCESS_SHIFT
#define ARG_GRAN_MASK L1ENTRY_ATTR_GRAN(SMPT_GRAN_ATTR, L1ENTRY_ATTR_GRAN_MASK)
#define ARG_LUT_PROT        mpt_prot_doubleword
#define T_FUNC(fn) s2mpu_v1 ## _ ##fn
#include <asm/s2mpu_mpt_template.h>
#include "test_kvm_s2mpu_template.h"
#undef ARG_PROT_BITS
#undef ARG_ACCESS_SHIFT
#undef ARG_GRAN_MASK
#undef ARG_LUT_PROT
#undef T_FUNC

#define ARG_PROT_BITS	V9_MPT_PROT_BITS
#define ARG_ACCESS_SHIFT V9_MPT_ACCESS_SHIFT
#define ARG_GRAN_MASK L1ENTRY_ATTR_GRAN(SMPT_GRAN_ATTR, V9_L1ENTRY_ATTR_GRAN_MASK)
#define ARG_LUT_PROT        v9_mpt_prot_doubleword
#define T_FUNC(fn) s2mpu_v9 ## _ ##fn
#include <asm/s2mpu_mpt_template.h>
#include "test_kvm_s2mpu_template.h"
#undef ARG_PROT_BITS
#undef ARG_ACCESS_SHIFT
#undef ARG_GRAN_MASK
#undef ARG_LUT_PROT
#undef T_FUNC

static void __init selftest(void)
{
	pr_info("Testing V9");
	s2mpu_v9_run_tests();
	pr_info("Testing V1-V2");
	s2mpu_v1_run_tests();
}

KSTM_MODULE_LOADERS(test_kvm_s2mpu);
MODULE_AUTHOR("David Brazdil <dbrazdil@google.com>");
MODULE_LICENSE("GPL v2");
