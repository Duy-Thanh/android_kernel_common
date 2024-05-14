#include <asm/kvm_pkvm_module.h>
#include <linux/arm-smccc.h>

static bool guest_proxy_smc(struct user_pt_regs *r)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(r->regs[0], r->regs[1], r->regs[2], r->regs[3],
			  r->regs[4], r->regs[5], r->regs[6], r->regs[7],
			  &res);
	return res.a0 == 0;
}

int pkvm_guest_smc_proxy_hyp_init(const struct pkvm_module_ops *ops)
{
	return ops->register_guest_smc_handler(guest_proxy_smc);
}
