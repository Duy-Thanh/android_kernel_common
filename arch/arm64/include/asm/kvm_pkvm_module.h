/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ARM64_KVM_PKVM_MODULE_H__
#define __ARM64_KVM_PKVM_MODULE_H__

#include <asm/kvm_pgtable.h>
#include <linux/android_kabi.h>
#include <linux/export.h>

typedef void (*dyn_hcall_t)(struct kvm_cpu_context *);

enum pkvm_psci_notification {
	PKVM_PSCI_CPU_SUSPEND,
	PKVM_PSCI_SYSTEM_SUSPEND,
	PKVM_PSCI_CPU_ENTRY,
};

#ifdef CONFIG_MODULES
/**
 * struct pkvm_module_ops - pKVM modules callbacks
 * @create_private_mapping:	Map a memory region into the hypervisor private
 *				range. @haddr returns the virtual address where
 *				the mapping starts. It can't be unmapped. The
 *				memory stays accessible from the host.
 * @alloc_module_va:		Reserve a range of VA space in the hypervisor
 *				private range. This is handy for modules who
 *				need to map plugin code in a similar fashion to
 *				how pKVM maps module code. That space could also
 *				be used to map memory temporarily, when the
 *				fixmap granularity (PAGE_SIZE) is too small.
 * @map_module_page:		Used in conjunction with @alloc_module_va. When
 *				@is_protected is not set, the page is also
 *				unmapped from the host stage-2.
 * @register_serial_driver:	Register a driver for a serial interface. The
 *				framework only needs a single callback
 *				@hyp_putc_cb which is expected to print a single
 *				character.
 * @puts:			When a serial interface is registered, print a
 *				string.
 * @putx64:			When a serial interface is registered, print a
 *				64-bits number.
 * @fixmap_map:			Map a page in the per-CPU hypervisor fixmap.
 *				This intends to be used for temporary mappings
 *				in the hypervisor VA space.
 * @fixmap_unmap:		Unmap a page from the hypervisor fixmap.
 * @linear_map_early:		Map a large portion of memory into the
 *				hypervisor linear VA space. This intends to be
 *				used only for module bootstrap and must be
 *				unmapped before the host is deprivilged.
 * @linear_unmap_early:		See @linear_map_early.
 * @flush_dcache_to_poc:	Allow modules to perform CMOs. This is not a
 *				requirement for any other of the pkvm_module_ops
 *				callbacks.
 * @update_hcr_el2:		If a module wants to enable additional traps, on
 *				top of what pKVM already provides. This is
 *				persistent across power management cycles.
 * @update_hfgwtr_el2:		If a module wants to enable fine grain traps.
 * @register_host_perm_fault_handler:
 *				@cb is called whenever the host generates an
 *				abort with the fault status code Permission
 *				Fault. Returning -EPERM let pKVM handle the
 *				abort. This is handy when a module changes the
 *				host stage-2 permissions for certain pages.
 * @host_stage2_mod_prot:	Apply @prot to the page @pfn. This requires a
 *				permission fault handler (see
 *				@register_host_perm_fault_handler). If none is
 *				registered, pKVM will be unable to handle this
 *				fault and the CPU will be stuck in an infinite
 *				loop.
 * @host_stage2_get_leaf:	Query a page-table entry for the page @phys.
 * @register_host_smc_handler:	@cb is called whenever the host issues an SMC
 *				pKVM couldn't handle. If @cb returns false, the
 *				SMC will be forwarded to EL3.
 * @register_default_trap_handler:
 *				@cb is called whenever EL2 traps EL1 and pKVM
 *				has not handled it. If @cb returns false, the
 *				hypervisor will panic. This trap handler must be
 *				registered whenever changes are made to HCR
 *				(@update_hcr_el2) or HFGWTR
 *				(@update_hfgwtr_el2).
 * @register_illegal_abt_notifier:
 *				@cb is called whenever an illegal abort from the
 *				host happens. In any case, the abort will be
 *				injected back into the host.
 * @register_psci_notifier:	@cb is called whenever a PSCI events occurs.
 *				This intends to let the module knows about
 *				suspend/resume.
 * @register_hyp_panic_notifier:
 *				@cb is called whenever the hypervisor will
 *				panic. This is simply a polite notification.
 *				Nothing from now on can prevent the hypervisor
 *				to die.
 * @host_donate_hyp:		The page @pfn is unmapped from the host and
 *				full control is given to the hypervisor. Only
 *				@hyp_donate_host can revert the spell.
 * @hyp_donate_host:		See @host_donate_hyp.
 * @host_share_hyp:		The page @pfn will be shared between the host
 *				and the hypervisor. The caller must imediately
 *				call @pin_shared_mem to protect that page.
 * @host_unshare_hyp:		The page @pfn will be unshared and unmapped from
 *				the hypervisor. This can only succeed if
 *				@unpin_shared_mem has been called.
 * @pin_shared_mem:		See @host_share_hyp.
 * @unpin_shared_mem:		See @host_unshare_hyp.
 * @memcpy:			Same as kernel memcpy.
 * @memset:			Same as kernel memset.
 * @hyp_pa:			Convert an hypervisor virtual address into a
 *				physical one. Does not work with addresses from
 *				the private range.
 * @hyp_va:			Convert a physical address into a virtual one.
 * @kern_hyp_va:		Convert a kernel virtual address into an
 *				hypervisor virtual one.
 */
struct pkvm_module_ops {
	int (*create_private_mapping)(phys_addr_t phys, size_t size,
				      enum kvm_pgtable_prot prot,
				      unsigned long *haddr);
	void *(*alloc_module_va)(u64 nr_pages);
	int (*map_module_page)(u64 pfn, void *va, enum kvm_pgtable_prot prot, bool is_protected);
	int (*register_serial_driver)(void (*hyp_putc_cb)(char));
	void (*puts)(const char *str);
	void (*putx64)(u64 num);
	void *(*fixmap_map)(phys_addr_t phys);
	void (*fixmap_unmap)(void);
	void *(*linear_map_early)(phys_addr_t phys, size_t size, enum kvm_pgtable_prot prot);
	void (*linear_unmap_early)(void *addr, size_t size);
	void (*flush_dcache_to_poc)(void *addr, size_t size);
	void (*update_hcr_el2)(unsigned long set_mask, unsigned long clear_mask);
	void (*update_hfgwtr_el2)(unsigned long set_mask, unsigned long clear_mask);
	int (*register_host_perm_fault_handler)(int (*cb)(struct kvm_cpu_context *ctxt, u64 esr, u64 addr));
	int (*host_stage2_mod_prot)(u64 pfn, enum kvm_pgtable_prot prot);
	int (*host_stage2_get_leaf)(phys_addr_t phys, kvm_pte_t *ptep, u32 *level);
	int (*register_host_smc_handler)(bool (*cb)(struct kvm_cpu_context *));
	int (*register_default_trap_handler)(bool (*cb)(struct kvm_cpu_context *));
	int (*register_illegal_abt_notifier)(void (*cb)(struct kvm_cpu_context *));
	int (*register_psci_notifier)(void (*cb)(enum pkvm_psci_notification, struct kvm_cpu_context *));
	int (*register_hyp_panic_notifier)(void (*cb)(struct kvm_cpu_context *host_ctxt));
	int (*host_donate_hyp)(u64 pfn, u64 nr_pages);
	int (*hyp_donate_host)(u64 pfn, u64 nr_pages);
	int (*host_share_hyp)(u64 pfn);
	int (*host_unshare_hyp)(u64 pfn);
	int (*pin_shared_mem)(void *from, void *to);
	void (*unpin_shared_mem)(void *from, void *to);
	void* (*memcpy)(void *to, const void *from, size_t count);
	void* (*memset)(void *dst, int c, size_t count);
	phys_addr_t (*hyp_pa)(void *x);
	void* (*hyp_va)(phys_addr_t phys);
	unsigned long (*kern_hyp_va)(unsigned long x);

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
	ANDROID_KABI_RESERVE(5);
	ANDROID_KABI_RESERVE(6);
	ANDROID_KABI_RESERVE(7);
	ANDROID_KABI_RESERVE(8);
	ANDROID_KABI_RESERVE(9);
	ANDROID_KABI_RESERVE(10);
	ANDROID_KABI_RESERVE(11);
	ANDROID_KABI_RESERVE(12);
	ANDROID_KABI_RESERVE(13);
	ANDROID_KABI_RESERVE(14);
	ANDROID_KABI_RESERVE(15);
	ANDROID_KABI_RESERVE(16);
	ANDROID_KABI_RESERVE(17);
	ANDROID_KABI_RESERVE(18);
	ANDROID_KABI_RESERVE(19);
	ANDROID_KABI_RESERVE(20);
	ANDROID_KABI_RESERVE(21);
	ANDROID_KABI_RESERVE(22);
	ANDROID_KABI_RESERVE(23);
	ANDROID_KABI_RESERVE(24);
	ANDROID_KABI_RESERVE(25);
	ANDROID_KABI_RESERVE(26);
	ANDROID_KABI_RESERVE(27);
	ANDROID_KABI_RESERVE(28);
	ANDROID_KABI_RESERVE(29);
	ANDROID_KABI_RESERVE(30);
	ANDROID_KABI_RESERVE(31);
	ANDROID_KABI_RESERVE(32);
};

int __pkvm_load_el2_module(struct module *this, unsigned long *token);

int __pkvm_register_el2_call(unsigned long hfn_hyp_va);
#else
static inline int __pkvm_load_el2_module(struct module *this,
					 unsigned long *token)
{
	return -ENOSYS;
}

static inline int __pkvm_register_el2_call(unsigned long hfn_hyp_va)
{
	return -ENOSYS;
}
#endif /* CONFIG_MODULES */

int pkvm_load_early_modules(void);

#ifdef MODULE
/*
 * Convert an EL2 module addr from the kernel VA to the hyp VA
 */
#define pkvm_el2_mod_va(kern_va, token)					\
({									\
	unsigned long hyp_text_kern_va =				\
		(unsigned long)THIS_MODULE->arch.hyp.text.start;	\
	unsigned long offset;						\
									\
	offset = (unsigned long)kern_va - hyp_text_kern_va;		\
	token + offset;							\
})

#define pkvm_load_el2_module(init_fn, token)				\
({									\
	THIS_MODULE->arch.hyp.init = init_fn;				\
	__pkvm_load_el2_module(THIS_MODULE, token);			\
})

#define pkvm_register_el2_mod_call(hfn, token)				\
({									\
	__pkvm_register_el2_call(pkvm_el2_mod_va(hfn, token));		\
})

#define pkvm_el2_mod_call(id, ...)					\
	({								\
		struct arm_smccc_res res;				\
									\
		arm_smccc_1_1_hvc(KVM_HOST_SMCCC_ID(id),		\
				  ##__VA_ARGS__, &res);			\
		WARN_ON(res.a0 != SMCCC_RET_SUCCESS);			\
									\
		res.a1;							\
	})
#endif
#endif
