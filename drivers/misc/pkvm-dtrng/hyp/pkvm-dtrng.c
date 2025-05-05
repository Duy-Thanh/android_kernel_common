// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */

#include <crypto/sha256_base.h>
#include <asm/kvm_pkvm_module.h>
#include <linux/arm-smccc.h>
#include <nvhe/spinlock.h>

#include "drbg.h"
#include "dtrng-common.h"

hyp_spinlock_t gen_lock;
struct drbg256_generator gen;

static int feed(void *addr, size_t size)
{
	hyp_spin_lock(&gen_lock);
	drbg256_seed(&gen, addr, size, NULL, 0, NULL, 0);
	hyp_spin_unlock(&gen_lock);

	return 0;
}

void pkvm_dtrng_feed_hvc(struct user_pt_regs *regs)
{
	int ret = -EINVAL;
	phys_addr_t addr = regs->regs[1];
	size_t size = regs->regs[2];
	void *hyp_addr;

	if (!mod_ops)
		goto exit;

	if (size > PAGE_SIZE) {
		mod_error("size too big");
		goto exit;
	}

	hyp_addr = hyp_fixmap_map(addr);
	if (!hyp_addr) {
		mod_error("fixmap_map failed");
		goto exit;
	}

	ret = feed(hyp_addr, size);

	hyp_fixmap_unmap();
exit:
	regs->regs[0] = SMCCC_RET_SUCCESS;
	regs->regs[1] = ret;
}

static int generate_locked(u8 *out, size_t size)
{
	union {
		int cpu;
		unsigned long virt_clk;
		u64 counter;
	} data;

	/* It's constant */
	data.cpu = _hyp_smp_processor_id();
	data.virt_clk = read_sysreg(cntvct_el0);
	data.counter = gen.req_counter;

	return drbg256_generate(&gen, (void *)&data, sizeof(data), out, size);
}

static bool handle_guest_hvc(struct arm_smccc_1_2_regs *r,
			     struct arm_smccc_res *res)
{
	unsigned long out[3] = { 0, 0, 0 };
	size_t count;
	int ret;

	switch (r->a0) {
	case ARM_SMCCC_TRNG_VERSION:
		mod_debug("handle_guest_hvc ARM_SMCCC_TRNG_VERSION");
		res->a0 = 1 << 16 | 0; // Version 1.0
		return true;
	case ARM_SMCCC_TRNG_FEATURES:
		switch (r->a1) {
		case ARM_SMCCC_TRNG_VERSION:
		case ARM_SMCCC_TRNG_FEATURES:
		/*TODO: case ARM_SMCCC_TRNG_GET_UUID:*/
		case ARM_SMCCC_TRNG_RND32:
		case ARM_SMCCC_TRNG_RND64:
			res->a0 = SMCCC_RET_SUCCESS;
		}
		return true;
	case ARM_SMCCC_TRNG_RND32:
		fallthrough;
	case ARM_SMCCC_TRNG_RND64:
		count = (r->a1 + 0x07) >> 3;
		count = max(count, sizeof(out));

		if (count != 0) {
			hyp_spin_lock(&gen_lock);
			ret = generate_locked((u8 *)out, count);
			hyp_spin_unlock(&gen_lock);
		} else {
			ret = -EINVAL;
		}

		if (ret == 0) {
			res->a0 = SMCCC_RET_SUCCESS;
			res->a1 = out[0];
			res->a2 = out[1];
			res->a3 = out[2];
		} else {
			mod_error("ARM_SMCCC_TRNG_RND NO_ENTROPY");
			res->a0 = -3 /* NO_ENTROPY */;
		}
		return true;
	default:
		return false;
	}
}

const struct pkvm_module_ops *mod_ops;

int pkvm_dtrng_init(const struct pkvm_module_ops *ops)
{
	int ret;

	mod_ops = ops;

	mod_debug("pKVM DTRNG driver loaded");

	hyp_spin_lock_init(&gen_lock);

	ret = __pkvm_register_guest_hvc_handler(handle_guest_hvc);
	if (ret) {
		mod_error("register_host_smc_handler failed returned:");
		hyp_putx64(ret);
		return ret;
	}

	return 0;
}

void *memcpy(void *to, const void *from, size_t count)
{
	return CALL_FROM_OPS(memcpy, to, from, count);
}

void *memset(void *dst, int c, size_t count)
{
	return CALL_FROM_OPS(memset, dst, c, count);
}
