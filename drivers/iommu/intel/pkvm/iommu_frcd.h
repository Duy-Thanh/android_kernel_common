/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 Google LLC
 */

#ifndef __PKVM_IOMMU_FRCD_H
#define __PKVM_IOMMU_FRCD_H

#include <linux/types.h>

struct intel_iommu;

/*
 * Sub-register types within a 128-bit Fault Recording Register (FRCD).
 *
 * Per VT-d spec 11.4.7.6, each FRCD is 16 bytes (128 bits):
 *   +0  (64-bit, FRCD_LO):    FI[63:12] fault address (ROS), RsvdZ[11:0]
 *   +8  (32-bit, FRCD_HI_LO): PP[31], EXE[30], PRIV[29], T2[28],
 *                              SID[15:0] (ROS), RsvdZ[27:16]
 *   +12 (32-bit, FRCD_HI_HI): F[31](RW1CS), T1[30], AT[29:28], PV[27:8],
 *                              FR[7:0] -> F is RW1CS, rest ROS
 *   +8  (64-bit, FRCD_HI):    64-bit access covering FRCD_HI_LO + FRCD_HI_HI
 */
enum pkvm_iommu_frcd_reg {
	PKVM_IOMMU_FRCD_REG_NONE,
	PKVM_IOMMU_FRCD_REG_LO,        /* +0: FI (fault address), all ROS */
	PKVM_IOMMU_FRCD_REG_HI_LO,     /* +8 (32-bit): SID/PP/EXE/PRIV/T2, all ROS */
	PKVM_IOMMU_FRCD_REG_HI_HI,     /* +12 (32-bit): F(RW1CS)/T1/AT/PV/FR */
	PKVM_IOMMU_FRCD_REG_HI,        /* +8 (64-bit): full FRCD_HI word */
};

struct pkvm_iommu_frcd_reg_info {
	enum pkvm_iommu_frcd_reg type;
};

bool iommu_frcd_reg_info(struct intel_iommu *iommu, unsigned long offset,
			 int len, struct pkvm_iommu_frcd_reg_info *info);
int iommu_frcd_validate_read(struct intel_iommu *iommu,
			     const struct pkvm_iommu_frcd_reg_info *info);
int iommu_frcd_validate_write(struct intel_iommu *iommu,
			      const struct pkvm_iommu_frcd_reg_info *info,
			      u64 val);

#endif /* __PKVM_IOMMU_FRCD_H */
