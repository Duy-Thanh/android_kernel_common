//SPDX-License-Identifier: GPL-2.0

#include <asm/kvm_hyp.h>
#include <asm/kvm_pkvm_module.h>

#include "example_heap.h"

static const struct pkvm_module_ops *ops;

void protect_page(struct user_pt_regs *regs)
{
	regs->regs[0] = SMCCC_RET_SUCCESS;
	regs->regs[1] = ops->host_stage2_mod_prot(regs->regs[1], 0, regs->regs[2], true);
}

void unprotect_page(struct user_pt_regs *regs)
{
	regs->regs[0] = SMCCC_RET_SUCCESS;
	regs->regs[1] = ops->host_stage2_mod_prot(regs->regs[1], KVM_PGTABLE_PROT_RWX,
						  regs->regs[2], true);
}

#define SMC_ACCEPT_SECURE_BUF	0x123

static enum pkvm_smc_handler_ret smc_handler(struct arm_smccc_1_2_regs *regs,
					     struct arm_smccc_1_2_regs *res,
					     pkvm_handle_t handle)
{
	u64 ipa, nr_pages;
	int ret;

	ops->memcpy(res, regs, sizeof(*res));

	if (regs->a0 != SMC_ACCEPT_SECURE_BUF)
		return GUEST_SMC_NOT_HANDLED;

	/* TODO: validate guest identity using @handle !*/

	ipa = regs->a1;
	nr_pages = regs->a2;
	ret = ops->guest_accept_module_prot_page(ipa, nr_pages);
	if (ret == -ENOMEM)
		return GUEST_SMC_NEED_TOPUP;

	res->a0 = (u64)ret;

	return GUEST_SMC_HANDLED;
}

static int module_owned_fault_handler(u64 phys, u64 ipa, u64 size, pkvm_handle_t handle)
{
	/* TODO: check the pfn ! */

	return 0;
}

int hyp_init(const struct pkvm_module_ops *__ops)
{
	int ret;

	ops = __ops;

	ret = ops->register_guest_smc_handler(smc_handler);
	if (ret)
		return ret;

	ret = ops->register_guest_accept_module_owned_handler(module_owned_fault_handler);
	if (ret)
		return ret;

	return 0;
}
