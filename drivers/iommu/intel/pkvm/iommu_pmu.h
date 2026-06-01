/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __PKVM_IOMMU_PMU_H
#define __PKVM_IOMMU_PMU_H

#include <linux/types.h>

struct intel_iommu;

void iommu_pmu_init(struct intel_iommu *iommu);

/*
 * Validate a host MMIO read/write to a PMU register at @offset/@len.
 *
 * Returns 0 if the access targets a PMU register and is permitted (the
 * caller may pass it to hardware), IOMMU_REG_NOT_HANDLED if @offset is
 * not a PMU register (the caller should try the next
 * handler / deny-by-default), or another negative errno if it is a PMU
 * register but the access is rejected.
 */
int iommu_pmu_validate_read(struct intel_iommu *iommu, unsigned long offset,
			    int len);
int iommu_pmu_validate_write(struct intel_iommu *iommu, unsigned long offset,
			     int len, u64 val);

#endif
