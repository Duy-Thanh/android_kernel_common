//SPDX-License-Identifier: GPL-2.0

#include "asm-generic/int-ll64.h"
#include "asm/kvm_pgtable.h"
#include "linux/arm-smccc.h"
#include "linux/compiler.h"
#include <asm/kvm_hyp.h>
#include <asm/kvm_pkvm_module.h>
#include <linux/align.h>
#include <linux/overflow.h>
#include <asm/pgtable-hwdef.h>

#include "example_heap.h"

static const struct pkvm_module_ops *ops;
static struct module_heap_config vm_state[MAX_CONFIGURED_VMS];
struct example_heap_config ex_heap_config;

static u64 constant_time_token_neq(const struct arm_smccc_1_2_regs* regs, const u64* exp)
{
	u64 diff = 0;

	diff |= regs->a3  ^ exp[0];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a4  ^ exp[1];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a5  ^ exp[2];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a6  ^ exp[3];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a7  ^ exp[4];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a8  ^ exp[5];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a9  ^ exp[6];
	OPTIMIZER_HIDE_VAR(diff);
	diff |= regs->a10 ^ exp[7];
	OPTIMIZER_HIDE_VAR(diff);

	return diff != 0;
}


static void module_zero_pages(u64 phys, u64 size)
{
	while (size > 0) {
		size_t map_size;
		void *vaddr = NULL;

		/* Fast path if we happen to have a PMD aligned large chunk. */
		if (IS_ALIGNED(phys, PMD_SIZE) && size >= PMD_SIZE) {
			vaddr = ops->fixblock_map(phys, &map_size);
			if (vaddr) {
				(ops->memset)(vaddr, 0, map_size);
				ops->fixblock_unmap();

				phys += map_size;
				size -= map_size;
				continue;
			}
		}

		/* Otherwise fall back to per-page poisoning. */
		map_size = PAGE_SIZE;
		vaddr = ops->fixmap_map(phys);
		if (vaddr) {
			(ops->memset)(vaddr, 0, map_size);
			ops->fixmap_unmap();
		}

		phys += map_size;
		size -= map_size;
	}
}

void protect_page(struct user_pt_regs *regs)
{
	u64 pfn = regs->regs[1];
	u64 nr_pages = regs->regs[2];
	u64 size, end;

	if (check_shl_overflow(nr_pages, PAGE_SHIFT, &size) ||
	    check_add_overflow((pfn << PAGE_SHIFT), size, &end)) {
		regs->regs[0] = SMCCC_RET_INVALID_PARAMETER;
		return;
	}

	regs->regs[1] = ops->host_stage2_mod_prot(pfn, 0, nr_pages, true);
	regs->regs[0] = SMCCC_RET_SUCCESS;
}

void unprotect_page(struct user_pt_regs *regs)
{
	u64 pfn = regs->regs[1];
	u64 nr_pages = regs->regs[2];
	u64 size, end;

	if (check_shl_overflow(nr_pages, PAGE_SHIFT, &size) ||
	    check_add_overflow((pfn << PAGE_SHIFT), size, &end)) {
		regs->regs[0] = SMCCC_RET_INVALID_PARAMETER;
		return;
	}

	module_zero_pages((pfn << PAGE_SHIFT), size);

	regs->regs[1] = ops->host_stage2_mod_prot(
		pfn, KVM_PGTABLE_PROT_RWX, nr_pages, true);

	regs->regs[0] = SMCCC_RET_SUCCESS;
}

/*  TODO: properly mark as 64 bit fast call */
#define SMC_ACCEPT_SECURE_BUF	0x123

static enum pkvm_smc_handler_ret smc_handler(struct arm_smccc_1_2_regs *regs,
					     struct arm_smccc_1_2_regs *res,
					     pkvm_handle_t handle)
{
	u64 ipa, nr_pages;
	int ret;
	int i;
	struct module_heap_config *matched_module_config = NULL;

	(ops->memcpy)(res, regs, sizeof(*res));

	if (regs->a0 != SMC_ACCEPT_SECURE_BUF)
		return GUEST_SMC_NOT_HANDLED;

	
	for (i = 0; i < ex_heap_config.num_entries; i ++) {
		struct module_heap_config *curr_config = &vm_state[i];
		u64* token = (u64*)curr_config->token_haddr;
		if (token && !constant_time_token_neq(regs, token)) {
			matched_module_config = curr_config;
		}
	}

	if (!matched_module_config) {
		res->a0 = -EACCES;
		return GUEST_SMC_HANDLED;
	}

	ipa = regs->a1;
	nr_pages = regs->a2;
	ret = ops->guest_accept_module_prot_page(ipa, nr_pages);
	if (ret == -ENOMEM)
		return GUEST_SMC_NEED_TOPUP;

	matched_module_config->bound_handle = handle;
	res->a0 = (u64)ret;

	return GUEST_SMC_HANDLED;
}

static struct module_heap_config *module_cfg_for_handle(pkvm_handle_t handle)
{
	int i;

	if (!handle)
		return NULL;

	for (i = 0; i < ex_heap_config.num_entries; i++) {
		if (handle == vm_state[i].bound_handle) {
			return &vm_state[i];
		}
	}

	return NULL;
}

static int module_owned_fault_handler(u64 phys, u64 ipa, u64 size,
				      pkvm_handle_t handle)
{
	struct module_heap_config *matched_config =
		module_cfg_for_handle(handle);

	/* pKVM will only call this fault handler if the provided handle and IPA have
	 * been marked as accepted in our smc_handler above. On VM destroy, module-owned
	 * page state is reset by pKVM. We can be sure that if we're handling a fault,
	 * matched_config is pointing to the most recently authenticated VM and that
	 * even if the handle is re-used, we can't reach this point again without another
	 * successful smc_handler call to accept the given IPA range.  */
	if (!matched_config)
		return -EPERM;

	/* TODO: Ensure page is correctly configured in the IOMMU as per the 
	 * protection_id of this VM. Also note that it's possible that another 
	 * guest relinquished this memory previously without poisoning the pages. 
	 * It is use-case dependent whether or not this module is expected to 
	 * zero pages before faulting them into a VM. We don't do so here to 
	 * support a theoretical write, secure, share pattern. But this may not 
	 * be desired for all use cases.*/

	return 0;
}

int hyp_init(const struct pkvm_module_ops *__ops)
{
	int ret;
	int i;

	ops = __ops;

	if (ex_heap_config.num_entries > MAX_CONFIGURED_VMS)
		return -EINVAL;

	for (i = 0; i < ex_heap_config.num_entries; i++) {
		struct heap_config_entry *config_entry =
			&ex_heap_config.entries[i];
		struct module_heap_config *module_entry = &vm_state[i];

		if (!config_entry->token_paddr)
			return -EINVAL;

		module_entry->config = config_entry;
		module_entry->bound_handle = 0;
		ret = ops->create_private_mapping(config_entry->token_paddr,
						  AUTH_TOKEN_SIZE,
						  PAGE_HYP_RO,
						  &module_entry->token_haddr);
		if (ret)
			return ret;
	}

	ret = ops->register_guest_smc_handler(smc_handler);
	if (ret)
		return ret;

	ret = ops->register_guest_accept_module_owned_handler(module_owned_fault_handler);
	if (ret)
		return ret;

	return 0;
}
