/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ARM_KVM_HOST_H__
#define __ARM_KVM_HOST_H__

enum kvm_mode {
	KVM_MODE_DEFAULT,
	KVM_MODE_PROTECTED,
	KVM_MODE_NV,
	KVM_MODE_NONE,
};

static inline enum kvm_mode kvm_get_mode(void) { return KVM_MODE_NONE; };
static inline bool kvm_skip_its_unmap(void) { return false; };
#endif /* __ARM_KVM_HOST_H__ */
