/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PKVM_IOMMU_PMU_H
#define __PKVM_IOMMU_PMU_H

#include <linux/types.h>

struct intel_iommu;

/*
 * PMU register classes understood by the pKVM MMIO validator. Some classes
 * identify fixed registers, while others identify entries inside dynamic
 * windows reported by the PMU offset registers.
 */
enum iommu_pmu_reg {
	IOMMU_PMU_REG_NONE,
	IOMMU_PMU_REG_EVENT_CAP,       /* fixed PERFEVNTCAP array */
	IOMMU_PMU_REG_CFG,             /* 64-bit PERFCNTRCFG */
	IOMMU_PMU_REG_FILTER_RID,      /* 32-bit requester-id filter */
	IOMMU_PMU_REG_FILTER_DID,      /* 32-bit domain-id filter */
	IOMMU_PMU_REG_FILTER_PASID,    /* 32-bit PASID filter */
	IOMMU_PMU_REG_FILTER_AT,       /* 32-bit address-type filter */
	IOMMU_PMU_REG_FILTER_PTL,      /* 32-bit page-table-level filter */
	IOMMU_PMU_REG_CNTR_CAP,        /* 32-bit PERFCNTRCAP */
	IOMMU_PMU_REG_CNTR_EVENT_CAP,  /* 32-bit per-counter event cap */
	IOMMU_PMU_REG_OVERFLOW,        /* 64-bit PERFOVFSTS */
	IOMMU_PMU_REG_COUNTER,         /* 64-bit PERFCNTR */
};

struct iommu_pmu_reg_info {
	enum iommu_pmu_reg type;
	u32 counter;
	u32 cntr_width;
	u32 filter_mask;
	bool cntr_ios;
	u64 writable_mask;
};

void iommu_pmu_init(struct intel_iommu *iommu);
bool iommu_pmu_get_reg_info(struct intel_iommu *iommu, unsigned long offset,
			    int len, struct iommu_pmu_reg_info *info);
int iommu_pmu_validate_read(struct intel_iommu *iommu,
			    struct iommu_pmu_reg_info *info);
int iommu_pmu_validate_write(struct intel_iommu *iommu,
			     struct iommu_pmu_reg_info *info, u64 val);

#endif
