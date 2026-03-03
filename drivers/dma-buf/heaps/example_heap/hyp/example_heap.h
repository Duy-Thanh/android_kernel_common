#ifndef __EXAMPLE_HEAP_H
#define __EXAMPLE_HEAP_H

#include <linux/types.h>

#define MAX_CONFIGURED_VMS 8

struct heap_config_entry {
	/* Physical address of the 64-byte secret */
	phys_addr_t token_paddr;
	/* An ID that can be used to validate a page's configuration in IOMMU page tables */
	u32 protection_id;
};

struct example_heap_config {
	u16 num_entries;
	struct heap_config_entry entries[MAX_CONFIGURED_VMS];
};

#ifdef __KVM_NVHE_HYPERVISOR__

extern struct example_heap_config ex_heap_config;

struct module_heap_config {
	const struct heap_config_entry *config;
	unsigned long token_haddr;
	pkvm_handle_t bound_handle;
};

int hyp_init(const struct pkvm_module_ops *__ops);
void protect_page(struct user_pt_regs *regs);
void unprotect_page(struct user_pt_regs *regs);
#else
int __kvm_nvhe_hyp_init(const struct pkvm_module_ops *__ops);
void __kvm_nvhe_protect_page(struct user_pt_regs *regs);
void __kvm_nvhe_unprotect_page(struct user_pt_regs *regs);
extern struct example_heap_config __kvm_nvhe_ex_heap_config;


extern unsigned long protect_page_hvc;
extern unsigned long unprotect_page_hvc;
extern const struct dma_heap_ops system_heap_modified_ops;

#define PKVM_VENDOR_IOC_MAGIC		'P'
#define PKVM_VENDOR_IOCTL_ENABLE_SMC	_IOWR(PKVM_VENDOR_IOC_MAGIC, 0x0, __u32)
#endif

#endif
