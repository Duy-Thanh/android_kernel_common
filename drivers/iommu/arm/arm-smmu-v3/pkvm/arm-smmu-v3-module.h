/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __ARM_SMMU_V3_MODULE__
#define __ARM_SMMU_V3_MODULE__

#if defined(__KVM_NVHE_HYPERVISOR__) && defined(MODULE)

#include <asm/kvm_pkvm_module.h>

extern const struct pkvm_module_ops		*mod_ops;

#define CALL_FROM_OPS(fn, ...)			mod_ops->fn(__VA_ARGS__)

#undef memset
#undef memcpy
#undef kvm_flush_dcache_to_poc
#undef kern_hyp_va

#define hyp_free(x)				CALL_FROM_OPS(hyp_free, x)
#define hyp_alloc_errno()			CALL_FROM_OPS(hyp_alloc_errno)
#define hyp_alloc(x)				CALL_FROM_OPS(hyp_alloc, x)
#define kvm_iommu_donate_pages(x, y)		CALL_FROM_OPS(iommu_donate_pages, x, y)
#define kvm_iommu_donate_pages_iopt(x, y, z)	CALL_FROM_OPS(iommu_donate_pages_iopt, x, y, z)
#define kvm_iommu_reclaim_pages(x, y)		CALL_FROM_OPS(iommu_reclaim_pages, x, y)
#define kvm_iommu_reclaim_pages_iopt(x, y, z)	CALL_FROM_OPS(iommu_reclaim_pages_iopt, x, y, z)
#define kvm_iommu_request(x)			CALL_FROM_OPS(iommu_request, x)
#define hyp_virt_to_phys(x)			CALL_FROM_OPS(hyp_pa, x)
#define hyp_phys_to_virt(x)			CALL_FROM_OPS(hyp_va, x)
#define pkvm_create_mappings(x, y, z)		CALL_FROM_OPS(create_mappings, x, y, z)
#define pkvm_create_hyp_device_mapping(x, y, z) CALL_FROM_OPS(create_hyp_device_mapping, x, y, z)
#define memcpy(x, y, z)				CALL_FROM_OPS(memcpy, x, y, z)
#define kvm_iommu_init_device(x)		CALL_FROM_OPS(iommu_init_device, x)
#define pkvm_udelay(x)				CALL_FROM_OPS(udelay, x)
#define kvm_flush_dcache_to_poc(x, y)		CALL_FROM_OPS(flush_dcache_to_poc, x, y)
#define hyp_alloc_missing_donations()		CALL_FROM_OPS(hyp_alloc_missing_donations)
#define __pkvm_host_donate_hyp(x, y)		CALL_FROM_OPS(host_donate_hyp, x, y)
#define hyp_fixmap_map(x)			CALL_FROM_OPS(fixmap_map, x)
#define hyp_fixmap_unmap()			CALL_FROM_OPS(fixmap_unmap)
#define kern_hyp_va(x)				CALL_FROM_OPS(kern_hyp_va, x)
#define kvm_iommu_iotlb_gather_add_page(x, y, z, w)  \
						CALL_FROM_OPS(iommu_iotlb_gather_add_page, \
							      x, y, z, w)
#endif

#endif /* __ARM_SMMU_V3_MODULE__ */
