// SPDX-License-Identifier: GPL-2.0
#include <asm/kvm_pkvm.h>
#include "gsmi.h"
#include "init.h"
#include "memory.h"

bool gsmi_present;
u16 smi_command_port;
phys_addr_t pkvm_gsmi_mem_base;
phys_addr_t pkvm_gsmi_mem_size;

static DEFINE_PER_CPU(union pkvm_gsmi_bounce_buf *, bounce_ptr);

static u16 gsmi_write(u8 subcmd, u32 param)
{
	u16 cmd = (subcmd << 8) | GSMI_CALLBACK;
	u16 result;

	/*
	 * See gsmi_exec() in drivers/firmware/google/gsmi.c for reference.
	 *
	 * Only support the GSMI_HANDSHAKE_NONE protocol, as that is what
	 * coreboot implements.
	 */
	asm volatile(
		"outb %%al, %%dx\n"
		: "=a" (result)
		: "0" (cmd),
		  "d" (smi_command_port),
		  "b" (param)
		: "memory", "cc"
		);

	return result;
}

int pkvm_init_gsmi(void)
{
	struct pkvm_pcpu *pcpu;
	int i;

	if (!gsmi_present)
		return 0;

	for_each_pkvm_pcpu(i, pcpu) {
		unsigned long offset = i * sizeof(union pkvm_gsmi_bounce_buf);

		if (offset + sizeof(union pkvm_gsmi_bounce_buf) > pkvm_gsmi_mem_size)
			return -ENOMEM;

		per_cpu(bounce_ptr, pcpu->cpu) = __pkvm_va(pkvm_gsmi_mem_base + offset);
	}

	return 0;
}

void pkvm_handle_gsmi(struct kvm_vcpu *vcpu)
{
	u8 subcmd = (vcpu->arch.regs[VCPU_REGS_RAX] >> 8) & 0xff;
	u32 param = vcpu->arch.regs[VCPU_REGS_RBX];
	u16 ret = GSMI_INVALID_PARAMETER;

	switch (subcmd) {
	case GSMI_CMD_SET_EVENT_LOG: {
		struct gsmi_set_eventlog_param *sel, *bounce_sel;
		struct gsmi_log_entry_type_1 *type1, *bounce_type1;

		if (pkvm_host_share_hyp(param, sizeof(*sel)))
			break;

		sel = __pkvm_va(param);

		if (sel->type != 1) {
			ret = GSMI_UNSUPPORTED;
			goto unshare_sel;
		}

		if (!sel->data_ptr || sel->data_len != sizeof(*type1))
			goto unshare_sel;

		if (pkvm_host_share_hyp(sel->data_ptr, sizeof(*type1)))
			goto unshare_sel;

		type1 = __pkvm_va(sel->data_ptr);

		bounce_sel = &this_cpu_read(bounce_ptr)->set_eventlog;
		bounce_type1 = &this_cpu_read(bounce_ptr)->log_entry;

		memcpy(bounce_sel, sel, sizeof(*sel));
		memcpy(bounce_type1, type1, sizeof(*type1));
		bounce_sel->data_ptr = __pkvm_pa(bounce_type1);

		ret = gsmi_write(subcmd, __pkvm_pa(bounce_sel));

		pkvm_host_unshare_hyp(sel->data_ptr, sizeof(*type1));
unshare_sel:
		pkvm_host_unshare_hyp(param, sizeof(*sel));
		break;
	}
	case GSMI_CMD_CLEAR_EVENT_LOG: {
		struct gsmi_clear_eventlog_param *cel, *bounce_cel;

		if (pkvm_host_share_hyp(param, sizeof(*cel)))
			break;

		cel = __pkvm_va(param);
		bounce_cel = &this_cpu_read(bounce_ptr)->clear_eventlog;
		memcpy(bounce_cel, cel, sizeof(*cel));

		ret = gsmi_write(subcmd, __pkvm_pa(bounce_cel));

		pkvm_host_unshare_hyp(param, sizeof(*cel));
		break;
	}
	case GSMI_CMD_LOG_S0IX_SUSPEND:
	case GSMI_CMD_LOG_S0IX_RESUME:
		ret = gsmi_write(subcmd, 0);
		break;
	case GSMI_CMD_HANDSHAKE_TYPE:
		ret = GSMI_HANDSHAKE_NONE;
		break;
	default:
		ret = GSMI_UNSUPPORTED;
		break;
	}

	vcpu->arch.regs[VCPU_REGS_RAX] = ret;
}
