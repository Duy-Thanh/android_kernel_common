#if !defined(__PKVM_PL011_HYPEVENTS_H_) || defined(HYP_EVENT_MULTI_READ)
#define __PKVM_PL011_HYPEVENTS_H_

#ifdef __KVM_NVHE_HYPERVISOR__
#include <trace.h>
#endif

HYP_EVENT(selftest,
	  HE_PROTO(void),
	  HE_STRUCT(),
	  HE_ASSIGN(),
	  HE_PRINTK(" ")
);
#endif
