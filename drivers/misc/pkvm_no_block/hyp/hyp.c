#include <asm/kvm_pkvm_module.h>
#include <asm/kvm_host.h>

const struct pkvm_module_ops *hyp_ops;

void pkvm_break_blocks(struct kvm_cpu_context *ctx)
{
	ctx->regs.regs[1] = hyp_ops->host_stage2_no_block();
}

int pkvm_driver_hyp_init(const struct pkvm_module_ops *ops)
{
	hyp_ops = ops;
	return 0;
}
