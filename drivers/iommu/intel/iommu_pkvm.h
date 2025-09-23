/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2025, Google.
 */

#ifndef _INTEL_IOMMU_PKVM_H_
#define _INTEL_IOMMU_PKVM_H_

#include <asm/kvm_pkvm.h>

#define pkvm_iommu_hypercall(hc, param_name, param)			\
	({									\
		unsigned long f;						\
		struct pkvm_##param_name *p = get_this_pv_param(param_name, f);	\
		int ret;							\
		*p = *(param);							\
		ret = pkvm_hypercall(hc, (unsigned long)p);		\
		*(param) = *p;							\
		put_this_pv_param(p, f);					\
		ret;								\
	})

static inline int pkvm_hc_qi_submit_sync(unsigned long reg_phys, unsigned long desc,
		unsigned int count)
{
	return pkvm_hypercall(iommu_submit_qi, reg_phys, desc, count);
}

static inline long pkvm_hc_iommu_clear_ce(struct pkvm_clear_translation_param *param)
{
	return pkvm_iommu_hypercall(iommu_clear_ce, clear_translation_param, param);
}

static inline long pkvm_hc_iommu_set_lm_ce(struct pkvm_lm_context_param *param)
{
	return pkvm_iommu_hypercall(iommu_set_lm_ce, lm_context_param, param);
}

static inline long pkvm_hc_iommu_set_sm_ce(struct pkvm_sm_context_param *param)
{
	return pkvm_iommu_hypercall(iommu_set_sm_ce, sm_context_param, param);
}

static inline long pkvm_hc_iommu_clear_pasid_entry(struct pkvm_clear_translation_param *param)
{
	return pkvm_iommu_hypercall(iommu_clear_pasid_entry, clear_translation_param, param);
}

static inline long pkvm_hc_iommu_set_pasid_fl(struct pkvm_pasid_table_param *param)
{
	return pkvm_iommu_hypercall(iommu_set_pasid_fl, pasid_table_param, param);
}

static inline long pkvm_hc_iommu_set_pasid_sl(struct pkvm_pasid_table_param *param)
{
	return pkvm_iommu_hypercall(iommu_set_pasid_sl, pasid_table_param, param);
}

static inline long pkvm_hc_iommu_domain_alloc(struct pkvm_domain_param *param)
{
	return pkvm_iommu_hypercall(iommu_domain_alloc, domain_param, param);
}

static inline long pkvm_hc_iommu_domain_free(u64 pgd_gpa)
{
	return pkvm_hypercall(iommu_domain_free, pgd_gpa);

}

static inline long pkvm_hc_iommu_map_pages(struct pkvm_iommu_map_param *param)
{
	return pkvm_iommu_hypercall(iommu_map_pages, iommu_map_param, param);
}

static inline long pkvm_hc_iommu_unmap_pages(unsigned long pgd_gpa,
					     unsigned long start_pfn, unsigned long last_pfn)
{
	return pkvm_hypercall(iommu_unmap_pages, pgd_gpa, start_pfn, last_pfn);
}
#endif
