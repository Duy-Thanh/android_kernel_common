#ifndef __EXAMPLE_HEAP_H
#define __EXAMPLE_HEAP_H

#ifdef __KVM_NVHE_HYPERVISOR__
int hyp_init(const struct pkvm_module_ops *__ops);
void protect_page(struct user_pt_regs *regs);
void unprotect_page(struct user_pt_regs *regs);
#else
int __kvm_nvhe_hyp_init(const struct pkvm_module_ops *__ops);
void __kvm_nvhe_protect_page(struct user_pt_regs *regs);
void __kvm_nvhe_unprotect_page(struct user_pt_regs *regs);


extern unsigned long protect_page_hvc;
extern unsigned long unprotect_page_hvc;
extern const struct dma_heap_ops system_heap_modified_ops;

#define PKVM_VENDOR_IOC_MAGIC		'P'
#define PKVM_VENDOR_IOCTL_ENABLE_SMC	_IOWR(PKVM_VENDOR_IOC_MAGIC, 0x0, __u32)
#endif

#endif
