/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2022 Intel Corporation. */

#ifndef _PKVM_IOMMU_DOMAIN_H_
#define _PKVM_IOMMU_DOMAIN_H_

#include "pkvm_hyp.h"

struct pkvm_iommu_domain_info {
	struct list_head node;
	struct pkvm_iommu *iommu;
	unsigned int refcnt;		/* Refcount of devices per iommu */
	unsigned int index;
	u16 did;			/* Domain ids per IOMMU. Use u16 since
					 * domain ids are 16 bit wide according
					 * to VT-d spec, section 9.3 */
};

/*
 * Represents a host iommu_domain/dmar_domain
 * Main function is to manage IO page tables.
 */
struct pkvm_iommu_domain {
	atomic_t refcount;
	unsigned long index;
	bool iommu_coherency;
	bool use_first_level;
	u64 pgd;
	int iommu_superpage;
	int gaw;
	int agaw;

	/*
	 * List of IOMMUs attached to this domain.
	 */
	struct list_head iommu_head;

	/*
	 * Lock to protect the mapping operations
	 * on this domain.
	 */
	pkvm_spinlock_t lock;

	struct hlist_node hnode;
};

#define DEFAULT_DOMAIN_ADDRESS_WIDTH 57

#define __DOMAIN_MAX_PFN(gaw)  ((((uint64_t)1) << ((gaw) - VTD_PAGE_SHIFT)) - 1)
#define __DOMAIN_MAX_ADDR(gaw) ((((uint64_t)1) << (gaw)) - 1)

/* We limit DOMAIN_MAX_PFN to fit in an unsigned long, and DOMAIN_MAX_ADDR
   to match. That way, we can use 'unsigned long' for PFNs with impunity. */
#define DOMAIN_MAX_PFN(gaw)	((unsigned long) min_t(uint64_t, \
				__DOMAIN_MAX_PFN(gaw), (unsigned long)-1))
#define DOMAIN_MAX_ADDR(gaw)	(((uint64_t)__DOMAIN_MAX_PFN(gaw)) << VTD_PAGE_SHIFT)

struct pkvm_iommu_domain *pkvm_alloc_iommu_domain(u64 pgd);
struct pkvm_iommu_domain *pkvm_get_iommu_domain(u64 pgd);
void pkvm_put_iommu_domain(struct pkvm_iommu_domain *iommu_domain);

unsigned long pkvm_domain_update_pgd(struct pkvm_iommu_domain *domain,
		struct pkvm_iommu_page_donation *donation, int agaw);
void pkvm_domain_attach_iommu(struct pkvm_iommu_domain *domain, struct pkvm_iommu *iommu, int did);
void pkvm_domain_detach_iommu(struct pkvm_iommu_domain *domain, struct pkvm_iommu *iommu);

unsigned long pkvm_iommu_domain_map(struct kvm_vcpu *hvcpu, unsigned long param_gva,
					unsigned long donation_gva);
unsigned long pkvm_iommu_domain_unmap(struct kvm_vcpu *hvcpu, unsigned long pgd_gpa,
					unsigned long start_pfn, unsigned long last_pfn,
					unsigned long donation_gva);
unsigned long pkvm_iommu_domain_alloc(struct kvm_vcpu *hvcpu, unsigned long param_gva);
#endif
