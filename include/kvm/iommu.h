/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KVM_IOMMU_H
#define __KVM_IOMMU_H

#include <asm/kvm_host.h>
#include <kvm/power_domain.h>
#include <linux/io-pgtable.h>

/*
 * Domain ID for identity mapped domain that the host can attach
 * to get the same mapping available to the CPU page table.
 */
#define KVM_IOMMU_DOMAIN_IDMAP_ID		0

/*
 * Nested domains must invalidate all stage-1 TLB entries when stage-2 mappings
 * change. If stage-1 only domains use the same VMID as nested domains, then
 * those TLB entries will also be invalidated when nested domains require
 * invalidation, which can negatively impact performance.
 *
 * Reserve VMID 1 so that nested domains don't share the same VMID as stage-1
 * only domains. This ensures that nested domain invalidations for stage-2 only
 * affect stage-1 TLB entries associated with nested domains.
 */
#define KVM_IOMMU_S1_DOMAIN_VMID		1

/* Used in alloc_domain type argument. */
#define KVM_IOMMU_DOMAIN_IDMAP_TYPE		0

#define KVM_IOMMU_DOMAIN_NR_START		(KVM_IOMMU_S1_DOMAIN_VMID + 1)

/**
 * struct kvm_hyp_iommu - Parameters from the trusted host:
 * @power_domain:	Power domain information
 * @lock:		Per iommu lock.
 * @power_is_off:	Current power status of the IOMMU.
 * Other members are filled and used at runtime by the IOMMU driver.
 */
struct kvm_hyp_iommu {
	struct kvm_power_domain		power_domain;
#ifndef __GENKSYMS__
	u32				lock;   /* lock size verified in kvm_iommu_get_lock.  */
#else
	u32				unused;
#endif
	bool				power_is_off;
	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
};

enum kvm_hyp_iommu_page_response_code {
	KVM_IOMMU_PAGE_RESP_SUCCESS = 0,
	KVM_IOMMU_PAGE_RESP_INVALID,
	KVM_IOMMU_PAGE_RESP_FAILURE,
};

static inline enum kvm_hyp_iommu_page_response_code
map_iommu_page_response_code(enum iommu_page_response_code code)
{
	if (code == IOMMU_PAGE_RESP_SUCCESS)
		return KVM_IOMMU_PAGE_RESP_SUCCESS;
	else if (code == IOMMU_PAGE_RESP_INVALID)
		return KVM_IOMMU_PAGE_RESP_INVALID;
	return KVM_IOMMU_PAGE_RESP_FAILURE;
}

/**
 * struct kvm_hyp_iommu_page_response - Generic page response information
 * @pasid: Process Address Space ID
 * @grpid: Page Request Group Index
 * @code: response code from &enum kvm_hyp_iommu_page_response_code
 */
struct kvm_hyp_iommu_page_response {
	u32 pasid;
	u32 grpid;
	u32 code;
};

extern void **kvm_nvhe_sym(kvm_hyp_iommu_domains);
#define kvm_hyp_iommu_domains kvm_nvhe_sym(kvm_hyp_iommu_domains)

/**
 * struct kvm_hyp_iommu_domain - KVM IOMMU domain:
 * @refs:		Refcount for the domain.
 * @domain_id:		ID for the domain (assigned from the kernel).
 * @priv:		Private pointer, typically points to driver specific struct.
 */
struct kvm_hyp_iommu_domain {
	atomic_t		refs;
	pkvm_handle_t		domain_id;
	void			*priv;
	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
};

/*
 * At the moment the number of domains is limited by the ASID and VMID size on
 * Arm. With single-stage translation, that size is 2^8 or 2^16. On a lot of
 * platforms the number of devices is actually the limiting factor and we'll
 * only need a handful of domains, but with PASID or SR-IOV support that limit
 * can be reached.
 *
 * In practice we're rarely going to need a lot of domains. To avoid allocating
 * a large domain table, we use a two-level table, indexed by domain ID. With
 * 4kB pages and 16-bytes domains, the leaf table contains 256 domains, and the
 * root table 256 pointers. With 64kB pages, the leaf table contains 4096
 * domains and the root table 16 pointers. In this case, or when using 8-bit
 * VMIDs, it may be more advantageous to use a single level. But using two
 * levels allows to easily extend the domain size.
 */
#define KVM_IOMMU_MAX_DOMAINS	(1 << 16)

/* Number of entries in the level-2 domain table */
#define KVM_IOMMU_DOMAINS_PER_PAGE \
	(PAGE_SIZE / sizeof(struct kvm_hyp_iommu_domain))

/* Number of entries in the root domain table */
#define KVM_IOMMU_DOMAINS_ROOT_ENTRIES \
	(KVM_IOMMU_MAX_DOMAINS / KVM_IOMMU_DOMAINS_PER_PAGE)

#define KVM_IOMMU_DOMAINS_ROOT_SIZE \
	(KVM_IOMMU_DOMAINS_ROOT_ENTRIES * sizeof(void *))

#endif /* __KVM_IOMMU_H */
