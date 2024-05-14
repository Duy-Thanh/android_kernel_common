#include <asm/kvm_pkvm_module.h>
#include <linux/arm-smccc.h>

static struct pkvm_module_ops *pkvm_ops;

static bool guest_proxy_smc(struct arm_smccc_1_2_regs *r)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_smc(r->a0, r->a1, r->a2, r->a3,
			  r->a4, r->a5, r->a6, r->a7,
			  &res);
	return res.a0 == 0;
}

int pkvm_guest_smc_proxy_hyp_init(const struct pkvm_module_ops *ops)
{
	pkvm_ops = ops;

	return 0;
}

void pkvm_set_guest_smc_trapping_hyp_hvc(struct user_pt_regs *r)
{
	if (!pkvm_ops)
		return;

	pkvm_ops->register_guest_smc_handler(guest_proxy_smc, r->regs[0]);
}
