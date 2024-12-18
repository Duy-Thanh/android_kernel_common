/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 - Google LLC
 * Author: Andrew Walbran <qwandor@google.com>
 */
#ifndef __KVM_HYP_FFA_H
#define __KVM_HYP_FFA_H

#include <asm/kvm_host.h>
#include <nvhe/pkvm.h>

#define FFA_MIN_FUNC_NUM 0x60
#define FFA_MAX_FUNC_NUM 0xFF

/* FF-A VM handle 1 is reserved for the host */
#define FFA_HANDLE_FROM_HYP_VCPU(hyp_vcpu)	((hyp_vcpu) == NULL ? 0 :\
	((((hyp_vcpu)->vcpu).kvm->arch.pkvm.handle) - HANDLE_OFFSET + 2))

struct ffa_mem_transfer {
	struct list_head node;
	u64 ffa_handle;
	struct list_head translations;
};

int hyp_ffa_init(void *pages);
bool kvm_host_ffa_handler(struct kvm_cpu_context *host_ctxt, u32 func_id);
bool kvm_guest_ffa_handler(struct pkvm_hyp_vcpu *hyp_vcpu, u64 *exit_code);
struct ffa_mem_transfer *find_transfer_by_handle(u64 ffa_handle, struct kvm_ffa_buffers *buf);
int kvm_reclaim_ffa_guest_pages(struct pkvm_hyp_vm *vm, pkvm_handle_t handle);
u32 ffa_get_hypervisor_version(void);

static inline bool is_ffa_call(u64 func_id)
{
        return ARM_SMCCC_IS_FAST_CALL(func_id) &&
               ARM_SMCCC_OWNER_NUM(func_id) == ARM_SMCCC_OWNER_STANDARD &&
               ARM_SMCCC_FUNC_NUM(func_id) >= FFA_MIN_FUNC_NUM &&
               ARM_SMCCC_FUNC_NUM(func_id) <= FFA_MAX_FUNC_NUM;
}

#endif /* __KVM_HYP_FFA_H */
