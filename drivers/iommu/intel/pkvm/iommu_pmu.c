// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Google LLC
 */
#include "pkvm/debug.h"
#include "iommu.h"
#include "perfmon.h"
#include "iommu_pmu.h"

/*
 * Validation of Performance Monitoring (PMU) registers for pKVM IOMMU MMIO
 * hardening.
 *
 * VT-d PMU registers are split between a fixed register block and register
 * windows whose offsets are reported by hardware through PERFCFGOFF,
 * PERFFRZOFF, PERFOVFOFF and PERFCNTROFF (VT-d spec 11.4.13).
 *
 * The host Intel IOMMU PMU driver uses these windows to discover event
 * capabilities, program counter configuration/filter registers, clear overflow
 * status and read/write counter values.  pKVM allows only those register slots
 * and validates writes against the architectural reserved bits and
 * capability-dependent fields.  Capability and status registers that are
 * read-only to software remain read-only here.
 */

/*
 * Verify a PMU register window fits in the remapping hardware MMIO region.
 * The offsets come from hardware registers and must not be trusted blindly.
 */
static bool iommu_pmu_window_in_range(struct intel_iommu *iommu,
				      u32 base, u64 size)
{
	return base <= iommu->reg_size && size <= iommu->reg_size - base;
}

static bool iommu_pmu_supported(struct intel_iommu *iommu, u64 *perfcap)
{
	if (!ecap_pms(iommu->ecap))
		return false;

	*perfcap = readq(iommu->reg + DMAR_PERFCAP_REG);
	return !!*perfcap;
}

static u32 iommu_pmu_num_counters(u64 perfcap)
{
	return min_t(u32, pcap_num_cntr(perfcap), IOMMU_PMU_IDX_MAX);
}

/*
 * Start with the global PERFCAP counter capabilities.  If per-counter
 * capabilities are present and enabled for @counter, override width and
 * interrupt/freeze support with that counter's PERFCNTRCAP value.
 */
static void iommu_pmu_counter_caps(struct intel_iommu *iommu, u64 perfcap,
				   u32 cfg, u32 counter,
				   struct pkvm_iommu_pmu_reg_info *info)
{
	u32 cap;

	info->cntr_width = pcap_cntr_width(perfcap);
	info->cntr_gfs = perfcap & BIT_ULL(49);
	info->cntr_ios = perfcap & BIT_ULL(50);

	if (!(perfcap & BIT_ULL(51)))
		return;

	cap = readl(iommu->reg + cfg + counter * IOMMU_PMU_CFG_OFFSET +
		    IOMMU_PMU_CFG_CNTRCAP_OFFSET);
	if (!iommu_cntrcap_pcc(cap))
		return;

	info->cntr_width = iommu_cntrcap_cw(cap);
	info->cntr_gfs = cap & BIT(17);
	info->cntr_ios = cap & BIT(16);
}

/**
 * iommu_pmu_reg_info - classify an MMIO access and check it targets a PMU reg.
 *
 * Returns true and fills @info when @offset/@len targets a supported PMU
 * register in either the fixed PMU capability block or one of the dynamic PMU
 * windows.  Returns false for unsupported access sizes, unaligned offsets, PMU
 * windows outside the MMIO region, and capability-dependent registers that are
 * not reported by hardware.
 */
bool iommu_pmu_reg_info(struct intel_iommu *iommu, unsigned long offset,
			int len, struct pkvm_iommu_pmu_reg_info *info)
{
	u32 cntr, num_cntr, num_eg, stride;
	u32 cfg, freeze, overflow, counter;
	u64 perfcap;

	memset(info, 0, sizeof(*info));

	if (!iommu_pmu_supported(iommu, &perfcap))
		return false;

	num_cntr = iommu_pmu_num_counters(perfcap);
	num_eg = pcap_num_event_group(perfcap);
	if (!num_cntr || !num_eg)
		return false;

	/*
	 * Global event capability registers are a fixed 64-bit array starting
	 * at PERFEVNTCAP_REG, one entry per event group.
	 */
	if (len == sizeof(u64) &&
	    offset >= DMAR_PERFEVNTCAP_REG &&
	    offset < DMAR_PERFEVNTCAP_REG + num_eg * IOMMU_PMU_CAP_REGS_STEP &&
	    IS_ALIGNED(offset - DMAR_PERFEVNTCAP_REG, IOMMU_PMU_CAP_REGS_STEP)) {
		info->type = PKVM_IOMMU_PMU_REG_EVENT_CAP;
		return true;
	}

	cfg = readl(iommu->reg + DMAR_PERFCFGOFF_REG);
	freeze = readl(iommu->reg + DMAR_PERFFRZOFF_REG);
	overflow = readl(iommu->reg + DMAR_PERFOVFOFF_REG);
	counter = readl(iommu->reg + DMAR_PERFCNTROFF_REG);
	stride = pcap_cntr_stride(perfcap);

	/*
	 * The spec defines the alignment of these dynamic PMU windows:
	 * configuration blocks are 256 bytes per counter, freeze and overflow
	 * status registers are 8-byte aligned, and counter alignment follows
	 * PERFCAP.CS.
	 */
	if (!IS_ALIGNED(cfg, 256) || !IS_ALIGNED(freeze, 8) ||
	    !IS_ALIGNED(overflow, 8) || !IS_ALIGNED(counter, stride))
		return false;

	if (!iommu_pmu_window_in_range(iommu, cfg, num_cntr * IOMMU_PMU_CFG_OFFSET) ||
	    !iommu_pmu_window_in_range(iommu, freeze, sizeof(u64)) ||
	    !iommu_pmu_window_in_range(iommu, overflow, sizeof(u64)) ||
	    !iommu_pmu_window_in_range(iommu, counter,
				       (num_cntr - 1) * stride + sizeof(u64)))
		return false;

	if (offset >= cfg && offset < cfg + num_cntr * IOMMU_PMU_CFG_OFFSET) {
		unsigned long cfg_off = offset - cfg;
		u32 reg_off = cfg_off % IOMMU_PMU_CFG_OFFSET;

		/*
		 * Each counter has a 256-byte configuration block.  Only the
		 * host driver-used registers in that block are allowed below.
		 */
		info->counter = cfg_off / IOMMU_PMU_CFG_OFFSET;
		info->filter_mask = pcap_filters_mask(perfcap);
		iommu_pmu_counter_caps(iommu, perfcap, cfg, info->counter, info);

		if (reg_off == 0) {
			if (len != sizeof(u64))
				return false;

			info->type = PKVM_IOMMU_PMU_REG_CFG;
			return true;
		}

		if (len != sizeof(u32))
			return false;

		switch (reg_off) {
		case IOMMU_PMU_CFG_SIZE:
			if (!(info->filter_mask & IOMMU_PMU_FILTER_REQUESTER_ID))
				return false;
			info->type = PKVM_IOMMU_PMU_REG_FILTER_RID;
			return true;
		case IOMMU_PMU_CFG_SIZE + IOMMU_PMU_CFG_FILTERS_OFFSET:
			if (!(info->filter_mask & IOMMU_PMU_FILTER_DOMAIN))
				return false;
			info->type = PKVM_IOMMU_PMU_REG_FILTER_DID;
			return true;
		case IOMMU_PMU_CFG_SIZE + 2 * IOMMU_PMU_CFG_FILTERS_OFFSET:
			if (!(info->filter_mask & IOMMU_PMU_FILTER_PASID))
				return false;
			info->type = PKVM_IOMMU_PMU_REG_FILTER_PASID;
			return true;
		case IOMMU_PMU_CFG_SIZE + 3 * IOMMU_PMU_CFG_FILTERS_OFFSET:
			if (!(info->filter_mask & IOMMU_PMU_FILTER_ATS))
				return false;
			info->type = PKVM_IOMMU_PMU_REG_FILTER_AT;
			return true;
		case IOMMU_PMU_CFG_SIZE + 4 * IOMMU_PMU_CFG_FILTERS_OFFSET:
			if (!(info->filter_mask & IOMMU_PMU_FILTER_PAGE_TABLE))
				return false;
			info->type = PKVM_IOMMU_PMU_REG_FILTER_PTL;
			return true;
		case IOMMU_PMU_CFG_CNTRCAP_OFFSET:
			if (!(perfcap & BIT_ULL(51)))
				return false;
			info->type = PKVM_IOMMU_PMU_REG_CNTR_CAP;
			return true;
		default:
			break;
		}

		/*
		 * Per-counter event capability registers are a 32-bit array
		 * starting at CNTREVCAP_OFFSET.  The array length can be
		 * reported by that counter's CNTRCAP.EGCNT field.
		 */
		if (perfcap & BIT_ULL(51)) {
			u32 cap = readl(iommu->reg + cfg +
					info->counter * IOMMU_PMU_CFG_OFFSET +
					IOMMU_PMU_CFG_CNTRCAP_OFFSET);
			u32 cntr_egcnt = iommu_cntrcap_egcnt(cap);

			if (!iommu_cntrcap_pcc(cap))
				return false;

			if (reg_off >= IOMMU_PMU_CFG_CNTREVCAP_OFFSET &&
			    reg_off < IOMMU_PMU_CFG_CNTREVCAP_OFFSET +
				      cntr_egcnt * IOMMU_PMU_OFF_REGS_STEP &&
			    IS_ALIGNED(reg_off - IOMMU_PMU_CFG_CNTREVCAP_OFFSET,
				       IOMMU_PMU_OFF_REGS_STEP)) {
				info->type = PKVM_IOMMU_PMU_REG_CNTR_EVENT_CAP;
				return true;
			}
		}

		return false;
	}

	/* Rest of the valid registers are 8 bytes in size */
	if (len != sizeof(u64))
		return false;

	if (offset == freeze) {
		info->type = PKVM_IOMMU_PMU_REG_FREEZE;
		return true;
	}

	if (offset == overflow) {
		info->type = PKVM_IOMMU_PMU_REG_OVERFLOW;
		/*
		 * PERFOVFSTS has one RW1C bit per counter.  Bits beyond the
		 * number of counters are reserved and must remain zero.
		 */
		info->writable_mask = num_cntr >= BITS_PER_TYPE(u64) ?
				      ~0ULL : GENMASK_ULL(num_cntr - 1, 0);
		return true;
	}

	for (cntr = 0; cntr < num_cntr; cntr++) {
		if (offset == counter + cntr * stride) {
			info->type = PKVM_IOMMU_PMU_REG_COUNTER;
			info->counter = cntr;
			iommu_pmu_counter_caps(iommu, perfcap, cfg, cntr, info);
			return true;
		}
	}

	return false;
}

int iommu_pmu_validate_read(struct intel_iommu *iommu,
			    struct pkvm_iommu_pmu_reg_info *info)
{
	if (info->type == PKVM_IOMMU_PMU_REG_NONE) {
		pkvm_err("iommu%d: invalid PMU register read\n", iommu->seq_id);
		return -EINVAL;
	}

	return 0;
}

int iommu_pmu_validate_write(struct intel_iommu *iommu,
			     struct pkvm_iommu_pmu_reg_info *info, u64 val)
{
	u32 v32 = (u32)val;

	switch (info->type) {
	case PKVM_IOMMU_PMU_REG_CFG:
		/*
		 * PERFCNTRCFG is writable, but most bits are reserved and the
		 * GFO/IO bits are valid only when reported for this counter.
		 */
		if (val & (GENMASK_ULL(63, 60) | GENMASK_ULL(31, 12) |
			   GENMASK_ULL(7, 3) | BIT_ULL(0))) {
			pkvm_err("iommu%d: PERFCNTRCFG 0x%llx has reserved bits set\n",
				 iommu->seq_id, val);
			return -EINVAL;
		}
		if ((val & BIT_ULL(2)) && !info->cntr_gfs) {
			pkvm_err("iommu%d: PERFCNTRCFG.GFO set without GFS support\n",
				 iommu->seq_id);
			return -EINVAL;
		}
		if ((val & BIT_ULL(1)) && !info->cntr_ios) {
			pkvm_err("iommu%d: PERFCNTRCFG.IO set without IOS support\n",
				 iommu->seq_id);
			return -EINVAL;
		}
		return 0;
	case PKVM_IOMMU_PMU_REG_FILTER_RID:
	case PKVM_IOMMU_PMU_REG_FILTER_DID:
		/* RID/DID filters use bits 15:0 and enable at bit 31. */
		if (v32 & GENMASK(30, 16)) {
			pkvm_err("iommu%d: PERFCNTR ID filter 0x%x has reserved bits set\n",
				 iommu->seq_id, v32);
			return -EINVAL;
		}
		return 0;
	case PKVM_IOMMU_PMU_REG_FILTER_PASID:
		/* PASID filters use PASID[19:0], PFM[1:0], and enable at bit 31. */
		if (v32 & GENMASK(30, 22)) {
			pkvm_err("iommu%d: PERFCNTR PASID filter 0x%x has reserved bits set\n",
				 iommu->seq_id, v32);
			return -EINVAL;
		}
		if (FIELD_GET(GENMASK(21, 20), v32) == 3) {
			pkvm_err("iommu%d: PERFCNTR PASID filter has reserved PFM encoding\n",
				 iommu->seq_id);
			return -EINVAL;
		}
		return 0;
	case PKVM_IOMMU_PMU_REG_FILTER_AT:
		/* AT filters use AT[4:0] except bit 3, plus enable at bit 31. */
		if (v32 & (GENMASK(30, 5) | BIT(3))) {
			pkvm_err("iommu%d: PERFCNTR AT filter 0x%x has reserved bits set\n",
				 iommu->seq_id, v32);
			return -EINVAL;
		}
		return 0;
	case PKVM_IOMMU_PMU_REG_FILTER_PTL:
		/* PTL filters use PTL[4:0] and enable at bit 31. */
		if (v32 & GENMASK(30, 5)) {
			pkvm_err("iommu%d: PERFCNTR PTL filter 0x%x has reserved bits set\n",
				 iommu->seq_id, v32);
			return -EINVAL;
		}
		return 0;
	case PKVM_IOMMU_PMU_REG_OVERFLOW:
		if (val & ~info->writable_mask) {
			pkvm_err("iommu%d: PERFOVFSTS 0x%llx has reserved bits set\n",
				 iommu->seq_id, val);
			return -EINVAL;
		}
		return 0;
	case PKVM_IOMMU_PMU_REG_COUNTER:
		/* Counter values are writable, but bits above the width are reserved. */
		if (!info->cntr_width || info->cntr_width > BITS_PER_TYPE(u64)) {
			pkvm_err("iommu%d: PERFCNTR has invalid width %u\n",
				 iommu->seq_id, info->cntr_width);
			return -EINVAL;
		}
		if (info->cntr_width < BITS_PER_TYPE(u64) &&
		    val & ~GENMASK_ULL(info->cntr_width - 1, 0)) {
			pkvm_err("iommu%d: PERFCNTR 0x%llx has reserved bits set\n",
				 iommu->seq_id, val);
			return -EINVAL;
		}
		return 0;
	case PKVM_IOMMU_PMU_REG_EVENT_CAP:
	case PKVM_IOMMU_PMU_REG_CNTR_CAP:
	case PKVM_IOMMU_PMU_REG_CNTR_EVENT_CAP:
	case PKVM_IOMMU_PMU_REG_FREEZE:
		pkvm_err("iommu%d: write to read-only PMU register blocked\n",
			 iommu->seq_id);
		return -EPERM;
	default:
		pkvm_err("iommu%d: invalid PMU register write\n", iommu->seq_id);
		return -EINVAL;
	}
}
