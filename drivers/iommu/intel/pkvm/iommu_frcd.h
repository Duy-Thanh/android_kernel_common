/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC
 */

#ifndef __PKVM_IOMMU_FRCD_H
#define __PKVM_IOMMU_FRCD_H

#include <linux/types.h>

struct intel_iommu;

/*
 * Validate a host MMIO read/write to a fault recording register at @offset/@len.
 *
 * Returns 0 if the access targets an FRCD sub-register and is permitted (the
 * caller may pass it to hardware), IOMMU_REG_NOT_HANDLED if @offset is not an
 * FRCD register (the caller should try the next handler / deny-by-default), or
 * another negative errno if it is an FRCD register but the access is rejected.
 */
int iommu_frcd_validate_read(struct intel_iommu *iommu, unsigned long offset,
			     int len);
int iommu_frcd_validate_write(struct intel_iommu *iommu, unsigned long offset,
			      int len, u64 val);

#endif /* __PKVM_IOMMU_FRCD_H */
