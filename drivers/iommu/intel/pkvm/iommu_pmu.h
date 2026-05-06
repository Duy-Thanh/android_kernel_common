/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PKVM_IOMMU_PMU_H
#define __PKVM_IOMMU_PMU_H

#include <linux/types.h>

struct intel_iommu;

/*
 * PMU register classes understood by the pKVM MMIO validator.  Some classes
 * identify fixed registers, while others identify entries inside dynamic
 * windows reported by the PMU offset registers.
 */
enum pkvm_iommu_pmu_reg {
	PKVM_IOMMU_PMU_REG_NONE,
	PKVM_IOMMU_PMU_REG_EVENT_CAP,       /* fixed PERFEVNTCAP array */
	PKVM_IOMMU_PMU_REG_CFG,             /* 64-bit PERFCNTRCFG */
	PKVM_IOMMU_PMU_REG_FILTER_RID,      /* 32-bit requester-id filter */
	PKVM_IOMMU_PMU_REG_FILTER_DID,      /* 32-bit domain-id filter */
	PKVM_IOMMU_PMU_REG_FILTER_PASID,    /* 32-bit PASID filter */
	PKVM_IOMMU_PMU_REG_FILTER_AT,       /* 32-bit address-type filter */
	PKVM_IOMMU_PMU_REG_FILTER_PTL,      /* 32-bit page-table-level filter */
	PKVM_IOMMU_PMU_REG_CNTR_CAP,        /* 32-bit PERFCNTRCAP */
	PKVM_IOMMU_PMU_REG_CNTR_EVENT_CAP,  /* 32-bit per-counter event cap */
	PKVM_IOMMU_PMU_REG_FREEZE,          /* 64-bit PERFFRZSTS */
	PKVM_IOMMU_PMU_REG_OVERFLOW,        /* 64-bit PERFOVFSTS */
	PKVM_IOMMU_PMU_REG_COUNTER,         /* 64-bit PERFCNTR */
};

struct pkvm_iommu_pmu_reg_info {
	enum pkvm_iommu_pmu_reg type;
	u32 counter;
	u32 cntr_width;
	u32 filter_mask;
	bool cntr_gfs;
	bool cntr_ios;
	u64 writable_mask;
};

bool iommu_pmu_reg_info(struct intel_iommu *iommu, unsigned long offset,
			int len, struct pkvm_iommu_pmu_reg_info *info);
int iommu_pmu_validate_read(struct intel_iommu *iommu,
			    struct pkvm_iommu_pmu_reg_info *info);
int iommu_pmu_validate_write(struct intel_iommu *iommu,
			     struct pkvm_iommu_pmu_reg_info *info, u64 val);

#endif
