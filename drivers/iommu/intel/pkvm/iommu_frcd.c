// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC
 */
#include "pkvm/debug.h"
#include "iommu.h"
#include "iommu_frcd.h"

/*
 * Validation of Fault Recording Registers (FRCD) for pKVM IOMMU MMIO hardening.
 *
 * The VT-d hardware logs non-recoverable DMA faults in a bank of 128-bit Fault
 * Recording Registers whose base offset and count are reported through
 * CAP.FRO and CAP.NFR (see spec section 11.4.2).
 *
 * These registers are architecturally read-only (ROS) for software except for
 * the single Fault (F) bit at position 127, which is RW1CS: software writes 1
 * to acknowledge and clear a logged fault.  All other bits are set by hardware
 * and must not be modified by software.
 *
 * To support the host IOMMU driver's fault-handling path, pKVM must allow:
 *   - reads of the fault address qword and the source/reason dwords
 *   - 32-bit write to the +12 word of an entry to clear the F bit only
 *   - 64-bit write to the +8 word of an entry to clear the F bit only
 *     (F occupies bit 63 of the 64-bit FRCD_HI view)
 * and must deny all other writes.
 */

/* Each fault recording register is 128 bits (16 bytes) */
#define FRCD_REG_SIZE	16

/* Byte offsets within one 16-byte FRCD entry */
#define FRCD_LO_OFF	0	/* 64-bit: fault address (FI), all ROS */
#define FRCD_HI_OFF	8	/* 64-bit: full HI word */
#define FRCD_HI_LO_OFF	8	/* 32-bit: SID, PP, EXE, PRIV, T2 - all ROS */
#define FRCD_HI_HI_OFF	12	/* 32-bit: F(RW1CS), T1, AT, PV, FR */

/*
 * Verify the entire FRCD array fits within the MMIO window.  The array starts
 * at byte offset @base and holds @num 16-byte entries.
 */
static bool iommu_frcd_window_in_range(struct intel_iommu *iommu,
				       u32 base, u32 num)
{
	u32 size = num * FRCD_REG_SIZE;

	return base <= iommu->reg_size && size <= iommu->reg_size - base;
}

/**
 * iommu_frcd_reg_info - classify an MMIO access and check it targets an FRCD.
 *
 * Returns true and fills @info when @offset/@len targets a valid FRCD
 * sub-register; returns false for any access outside the FRCD array or with
 * an unsupported access size or alignment.
 */
bool iommu_frcd_reg_info(struct intel_iommu *iommu, unsigned long offset,
			 int len, struct pkvm_iommu_frcd_reg_info *info)
{
	u32 fro, num_frcd, sub_off;

	memset(info, 0, sizeof(*info));

	num_frcd = cap_num_fault_regs(iommu->cap);
	fro = cap_fault_reg_offset(iommu->cap);

	/*
	 * FRO = 0 would place FRCD registers at offset 0 (overlapping the
	 * fixed Version Register). This is not a valid hardware configuration;
	 * treat it as "no FRCD present" rather than misidentifying fixed
	 * registers as fault records.
	 */
	if (!fro || !num_frcd)
		return false;

	if (!iommu_frcd_window_in_range(iommu, fro, num_frcd))
		return false;

	/* Is this offset inside the FRCD array? */
	if (offset < fro || offset >= fro + (u32)num_frcd * FRCD_REG_SIZE)
		return false;

	sub_off = (offset - fro) % FRCD_REG_SIZE;

	switch (len) {
	case sizeof(u32):
		switch (sub_off) {
		case FRCD_HI_LO_OFF:
			info->type = PKVM_IOMMU_FRCD_REG_HI_LO;
			return true;
		case FRCD_HI_HI_OFF:
			info->type = PKVM_IOMMU_FRCD_REG_HI_HI;
			return true;
		default:
			return false;
		}
	case sizeof(u64):
		switch (sub_off) {
		case FRCD_LO_OFF:
			info->type = PKVM_IOMMU_FRCD_REG_LO;
			return true;
		case FRCD_HI_OFF:
			info->type = PKVM_IOMMU_FRCD_REG_HI;
			return true;
		default:
			return false;
		}
	default:
		return false;
	}
}

int iommu_frcd_validate_read(struct intel_iommu *iommu,
			     const struct pkvm_iommu_frcd_reg_info *info)
{
	if (info->type == PKVM_IOMMU_FRCD_REG_NONE) {
		pkvm_err("iommu%d: invalid fault recording register read\n",
			 iommu->seq_id);
		return -EINVAL;
	}
	return 0;
}

int iommu_frcd_validate_write(struct intel_iommu *iommu,
			      const struct pkvm_iommu_frcd_reg_info *info,
			      u64 val)
{
	switch (info->type) {
	case PKVM_IOMMU_FRCD_REG_HI_HI:
		/*
		 * 32-bit write to FRCD_HI_HI (+12).  The F bit (bit 31) is
		 * RW1CS: software writes 1 to clear a logged fault.  Every
		 * other bit in this word is ROS and must not be touched.
		 */
		if ((u32)val & ~DMA_FRCD_F) {
			pkvm_err("iommu%d: FRCD write 0x%llx sets non-F bits\n",
				 iommu->seq_id, val);
			return -EINVAL;
		}
		return 0;

	case PKVM_IOMMU_FRCD_REG_HI:
		/*
		 * 64-bit write to FRCD_HI (+8).  In this view the F bit sits
		 * at bit 63 (the MSB of the 64-bit word).  The lower 32 bits
		 * cover the SID/PP/EXE/PRIV/T2 fields which are all ROS.
		 */
		if (val & ~BIT_ULL(63)) {
			pkvm_err("iommu%d: FRCD 64-bit write 0x%llx sets non-F bits\n",
				 iommu->seq_id, val);
			return -EINVAL;
		}
		return 0;

	case PKVM_IOMMU_FRCD_REG_LO:
	case PKVM_IOMMU_FRCD_REG_HI_LO:
		/*
		 * FRCD_LO (fault address) and FRCD_HI_LO (SID/PP/EXE/PRIV/T2)
		 * are entirely read-only (ROS).  Block all writes.
		 */
		pkvm_err("iommu%d: write to read-only FRCD register blocked\n",
			 iommu->seq_id);
		return -EPERM;
		break;
	default:
		pkvm_err("iommu%d: invalid fault recording register write\n",
			 iommu->seq_id);
		return -EINVAL;
	}
}
