/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025, Google.
 */

#ifndef _INTEL_IOMMU_PKVM_H_
#define _INTEL_IOMMU_PKVM_H_

#include <asm/pkvm.h>

static inline long pkvm_hc_enable_iommu(unsigned long reg_phys,
		unsigned long root_gpa)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_ENABLE_IOMMU,
				reg_phys, root_gpa);

	return ret;
}

static inline long pkvm_hc_disable_iommu(unsigned long reg_phys)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall1(PKVM_HC_DISABLE_IOMMU, reg_phys);

	return ret;
}

static inline long pkvm_hc_iommu_clear_ce(unsigned long reg_phys,
		struct pkvm_clear_translation_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_CLEAR_CE,
				reg_phys, virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_lm_ce(unsigned long reg_phys,
		struct pkvm_lm_context_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_LM_CE, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_sm_ce(unsigned long reg_phys,
		struct pkvm_sm_context_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_SM_CE, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_sm_ce_pre(unsigned long reg_phys,
		struct pkvm_sm_context_pre_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_SM_CE_PRE, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_clear_pasid_entry(unsigned long reg_phys,
		struct pkvm_clear_translation_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_CLEAR_PASID_ENTRY, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_pasid_fl(unsigned long reg_phys,
		struct pkvm_pasid_table_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_PASID_FL, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_pasid_sl(unsigned long reg_phys,
		struct pkvm_pasid_table_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_PASID_SL, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_domain_alloc(unsigned long reg_phys,
		struct pkvm_domain_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_DOMAIN_ALLOC, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_domain_free(u64 pgd_gpa)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall1(PKVM_HC_IOMMU_DOMAIN_FREE, pgd_gpa);

	return ret;
}

static inline long pkvm_hc_iommu_map_pages(struct pkvm_iommu_map_param *param,
		struct pkvm_iommu_page_donation *donation)
{
	long ret = 0;
	if (pkvm_pviommu_enabled()) {
		ret = kvm_hypercall2(PKVM_HC_IOMMU_MAP_PAGES,
				virt_to_phys(param), virt_to_phys(donation));
	}

	return ret;
}

static inline long pkvm_hc_iommu_unmap_pages(unsigned long pgd_gpa, unsigned long start_pfn,
		unsigned long last_pfn, struct pkvm_iommu_page_donation *donation)
{
	long ret = 0;
	if (pkvm_pviommu_enabled()) {
		ret = kvm_hypercall4(PKVM_HC_IOMMU_UNMAP_PAGES, pgd_gpa, start_pfn, last_pfn,
				virt_to_phys(donation));
	}

	return ret;
}
static inline long pkvm_hc_cache_tag_assign(unsigned long pgd_gpa)
{
	long ret = 0;
	if (pkvm_pviommu_enabled()) {
		ret = kvm_hypercall1(PKVM_HC_IOMMU_CACHE_ASSIGN, pgd_gpa);
	}

	return ret;
}

static inline long pkvm_hc_cache_tag_unassign(unsigned long pgd_gpa)
{
	long ret = 0;
	if (pkvm_pviommu_enabled()) {
		ret = kvm_hypercall1(PKVM_HC_IOMMU_CACHE_UNASSIGN, pgd_gpa);
	}

	return ret;
}

static inline int pkvm_hc_qi_submit_sync(unsigned long reg_phys, unsigned long desc,
		unsigned int count)
{
	if (pkvm_pviommu_enabled())
		kvm_hypercall3(PKVM_HC_SUBMIT_QI, reg_phys, desc, count);

	return 0;
}
#endif

