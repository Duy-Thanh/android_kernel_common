// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2026 Google.
 */
#include <asm/kvm_pkvm.h>
#include "pkvm/mmu.h"
#include "pkvm/memory.h"
#include "pkvm/pkvm.h"
#include "pkvm/debug.h"
#include "iommu.h"

/*
 * IOMMU supported page size and page levels for second stage page table.
 *
 * Here we set it to the maximum supported values and during IOMMU initialization,
 * we determine the least common values supported by all the IOMMUs in the system.
 */
unsigned int iommu_pgsz_mask = 1 << PG_LEVEL_4K | 1 << PG_LEVEL_2M | 1 << PG_LEVEL_1G;
unsigned int iommu_pglvl_mask = IOMMU_PGT_4LEVEL | IOMMU_PGT_5LEVEL;

/*
 * x86 MSI address base: all legitimate MSI interrupts target this range
 * (Local APIC delivery). FEADDR must have this prefix.
 */
#define X86_MSI_ADDR_BASE	0xfee00000U
#define X86_MSI_ADDR_MASK	0xfff00000U

/*
 * Check whether the 64-bit MSI address formed by combining the upper (uaddr)
 * and lower (addr) 32-bit halves falls within system memory.
 *
 * In xAPIC mode uaddr is zero, giving the standard 0xFEExxxxx range which
 * is never backed by DRAM.  In x2APIC mode uaddr carries an APIC cluster ID
 * (e.g. 0x100), producing an address such as 0x100_FEE00000 (~1 TiB) that
 * CAN reside in DRAM on machines with sufficient RAM.  Reject such values to
 * prevent the IOMMU from corrupting protected memory on interrupt delivery.
 */
static bool msi_addr_targets_memory(u32 addr, u32 uaddr)
{
	u64 msi_addr = ((u64)uaddr << 32) | addr;

	return is_memory_range(msi_addr, sizeof(u32));
}

/* GCMD bits that handle enabling/disabling of IOMMU features */
#define DMAR_GSTS_EN_BITS	(DMA_GCMD_TE | DMA_GCMD_QIE | DMA_GCMD_IRE | DMA_GCMD_CFI)
/* GCMD oneshot bits where unsetting the bit doesn't have an effect */
#define DMAR_GCMD_ONESHOT	(DMA_GCMD_SRTP | DMA_GCMD_SIRTP)
/* Mask of bits the host is allowed to access directly (passed through to hardware) */
#define DMAR_GCMD_DIRECT	(DMA_GCMD_IRE | DMA_GCMD_CFI)
/* Mask of bits supported by pKVM */
#define DMAR_GCMD_SUPPORTED_BITS	(DMAR_GSTS_EN_BITS | DMA_GCMD_SRTP | DMA_GCMD_SIRTP)

u16 satc_devs[PKVM_MAX_SATC_DEVS];
int nr_satc_devs;

#define PKVM_MAX_IOMMU_NUM	16
static struct intel_iommu iommus[PKVM_MAX_IOMMU_NUM];
static int nr_iommus;

/*
 * Flag denoting if all IOMMUs in the system have page walk coherency support.
 * This is needed to decide whether to flush cpu caches on host ept updates as
 * pKVM uses host ept as the second stage page table when host configures device
 * for passthrough mode. IOMMUs that doesn't have page walk coherency support
 * needs the pagetable updates to be reflected in memory and hence we need to
 * flush cpu caches on host ept update if one or more IOMMUs doesn't have page
 * walk coherency support.
 */
static bool iommu_paging_structure_coherent = true;

bool pkvm_iommu_paging_structure_coherency(void)
{
	return iommu_paging_structure_coherent;
}

bool is_dev_in_satc(u16 bdf)
{
	int i;

	for (i = 0; i < nr_satc_devs; i++) {
		if (bdf == satc_devs[i])
			return true;
	}
	return false;
}

struct intel_iommu *iommu_from_phys(unsigned long phys)
{
	int i;

	for (i = 0; i < nr_iommus; i++) {
		struct intel_iommu *iommu = &iommus[i];

		if (phys >= iommu->reg_phys && phys < (iommu->reg_phys + iommu->reg_size))
			return iommu;
	}

	return NULL;
}

bool overlaps_iommu_mmio(unsigned long phys, unsigned long size)
{
	int i;

	for (i = 0; i < nr_iommus; i++) {
		struct intel_iommu *iommu = &iommus[i];

		if (phys < (iommu->reg_phys + iommu->reg_size) &&
		    (phys + size) > iommu->reg_phys)
			return true;
	}
	return false;
}

static int iommu_direct_mmio_read(struct intel_iommu *iommu, u64 phys,
				  int len, u64 *val)
{
	unsigned long offset = phys - iommu->reg_phys;
	void *reg = iommu->reg + offset;

	switch (len) {
	case 4:
		*val = readl(reg);
		break;
	case 8:
		*val = readq(reg);
		break;
	default:
		pkvm_err("%s: unsupported len %d\n", __func__, len);
		return -EINVAL;
	}
	return 0;
}

static int iommu_direct_mmio_write(struct intel_iommu *iommu, u64 phys,
				   int len, u64 val)
{
	unsigned long offset = phys - iommu->reg_phys;
	void *reg = iommu->reg + offset;

	switch (len) {
	case 4:
		writel((u32)val, reg);
		break;
	case 8:
		writeq(val, reg);
		break;
	default:
		pkvm_err("%s: unsupported len %d\n", __func__, len);
		return -EINVAL;
	}
	return 0;
}

static int handle_gcmd_direct(struct intel_iommu *iommu, u32 gcmd_bit, bool set)
{
	u32 gcmd = iommu->vgsts & DMAR_GSTS_EN_BITS;
	u32 sts;

	if ((gcmd_bit & DMAR_GCMD_ONESHOT) && !set)
		return -EINVAL;

	if (set)
		gcmd |= gcmd_bit;
	else
		gcmd &= ~gcmd_bit;

	writel(gcmd, iommu->reg + DMAR_GCMD_REG);
	if (set)
		IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, readl, (sts & gcmd_bit), sts);
	else
		IOMMU_WAIT_OP(iommu, DMAR_GSTS_REG, readl, !(sts & gcmd_bit), sts);

	iommu->vgsts = (iommu->vgsts & DMAR_GCMD_ONESHOT) | gcmd;

	return 0;
}

static int initialize_qi(struct intel_iommu *iommu)
{
	u64 desc_sz = ecap_smts(iommu->ecap) ? SZ_8K : SZ_4K;
	struct q_inval *qi = iommu->qi;
	u64 val = __pkvm_pa(qi->desc);
	int ret;

	ret = pkvm_host_donate_hyp_share_ro(val, desc_sz, true);
	if (ret) {
		pkvm_err("iommu%d: failed to write protect QI desc!\n", iommu->seq_id);
		return ret;
	}

	iommu->flush.flush_context = qi_flush_context;
	iommu->flush.flush_iotlb = qi_flush_iotlb;

	pkvm_spin_lock_init(&qi->q_lock);
	qi->free_head = qi->free_tail = 0;
	qi->free_cnt = QI_LENGTH;

	/*
	 * Set DW=1 and QS=1 in IQA_REG when Scalable Mode capability
	 * is present.
	 */
	if (sm_supported(iommu))
		val |= BIT_ULL(11) | BIT_ULL(0);

	/* write zero to the tail reg */
	writel(0, iommu->reg + DMAR_IQT_REG);
	/* Set IQA */
	writeq(val, iommu->reg + DMAR_IQA_REG);

	return handle_gcmd_direct(iommu, DMA_GCMD_QIE, true);
}

static int handle_gcmd_qie(struct intel_iommu *iommu, bool enable)
{
	int ret = 0;

	if (enable) {
		if (iommu->qi || iommu->vgsts & DMA_GSTS_QIES) {
			pkvm_err("iommu%d: QI already enabled\n", iommu->seq_id);
			return -EBUSY;
		} else if (!iommu->viqa) {
			pkvm_err("iommu%d: QIE before setting IQA\n", iommu->seq_id);
			return -EINVAL;
		}

		/*
		 * Host IOMMU driver dynamically allocates iommu->qi, but pKVM has it
		 * embedded. For easy re-use of host code, the embedded field is named
		 * as iommu->_qi, and the pointer iommu->qi points to iommu->_qi.
		 * Also, it serves as a flag to denote whether qi is
		 * enabled(similar to how host driver does)
		 */
		iommu->qi = &iommu->_qi;
		iommu->qi->desc = pkvm_host_gpa_to_virt(iommu->viqa & VTD_PAGE_MASK);
		ret = initialize_qi(iommu);
	} else {
		if (!iommu->qi)
			ret = handle_gcmd_direct(iommu, DMA_GCMD_QIE, false);
		else
			iommu->vgsts &= ~DMA_GSTS_QIES;
	}

	pkvm_dbg("iommu%d: Quueued Invalidation %s!\n", iommu->seq_id,
		 enable ? "enabled" : "disabled");
	return ret;
}

static void set_root_table(struct intel_iommu *iommu)
{
	writeq(iommu->vrta, iommu->reg + DMAR_RTADDR_REG);
	handle_gcmd_direct(iommu, DMA_GCMD_SRTP, true);

	if (cap_esrtps(iommu->cap))
		return;

	iommu->flush.flush_context(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL);
	if (sm_supported(iommu))
		qi_flush_pasid_cache(iommu, 0, QI_PC_GLOBAL, 0);
	iommu->flush.flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);
}

static int handle_gcmd_srtp(struct intel_iommu *iommu)
{
	u32 gsts = readl(iommu->reg + DMAR_GSTS_REG);
	u64 root_pa;
	int ret;

	if (WARN_ON(gsts != iommu->vgsts))
		iommu->vgsts = gsts;

	if (!iommu->vrta) {
		pkvm_warn("iommu%d: host RTADDR_REG not set", iommu->seq_id);
		return -EINVAL;
	} else if (iommu->vgsts & DMA_GSTS_TES) {
		pkvm_warn("iommu%d: SRTP not allowed after TE", iommu->seq_id);
		return -EBUSY;
	} else if (iommu->root_entry) {
		pkvm_warn("iommu%d: SRTP allowed only once", iommu->seq_id);
		return -EBUSY;
	}

	root_pa = pkvm_host_gpa_to_phys(iommu->vrta & VTD_PAGE_MASK);
	ret = pkvm_host_donate_hyp_share_ro(root_pa, VTD_PAGE_SIZE, true);
	if (ret) {
		pkvm_err("iommu%d: failed to write protect root table page(err=%d)!\n",
			 iommu->seq_id, ret);
		return ret;
	}

	iommu->root_entry = __pkvm_va(root_pa);
	__iommu_flush_cache(iommu, iommu->root_entry, VTD_PAGE_SIZE);

	set_root_table(iommu);

	pkvm_dbg("iommu%d Set Root Table(%llx)!\n", iommu->seq_id, iommu->vrta);
	return 0;
}

/*
 * Donate the IR table to the hypervisor as read-only and record its virtual
 * address in iommu->ir_table.  Called both from iommu_init() (table already
 * live before deprivilege) and from handle_gcmd_sirtp() (host sets a new
 * table after deprivilege).
 */
static int iommu_protect_ir_table(struct intel_iommu *iommu)
{
	u64 ir_table_pa;
	int ret;

	if (!iommu->virta) {
		pkvm_err("iommu%d: IR table protection requested before IRTA set\n",
			 iommu->seq_id);
		return -EINVAL;
	}

	ir_table_pa = pkvm_host_gpa_to_phys(iommu->virta & VTD_PAGE_MASK);
	ret = pkvm_host_donate_hyp_share_ro(ir_table_pa, SZ_1M, false);
	if (ret) {
		pkvm_err("iommu%d: failed to write protect IR table[%llx] (err=%d)\n",
			 iommu->seq_id, ir_table_pa, ret);
		return ret;
	}

	iommu->ir_table = __pkvm_va(ir_table_pa);
	__iommu_flush_cache(iommu, iommu->ir_table, SZ_1M);

	return 0;
}

static int handle_gcmd_sirtp(struct intel_iommu *iommu)
{
	int ret;

	if (iommu->ir_table) {
		pkvm_err("iommu%d: SIRTP allowed only once\n", iommu->seq_id);
		return -EBUSY;
	}

	ret = iommu_protect_ir_table(iommu);
	if (ret)
		return ret;

	writeq(iommu->virta, iommu->reg + DMAR_IRTA_REG);
	handle_gcmd_direct(iommu, DMA_GCMD_SIRTP, true);

	/*
	 * Flush the interrupt entry cache globally if the hardware doesn't
	 * support Enhanced SIRTP.
	 */
	if (iommu->qi && !cap_esirtps(iommu->cap))
		qi_global_iec(iommu);

	return 0;
}

static int handle_gcmd_te(struct intel_iommu *iommu, bool enable)
{
	if (enable) {
		if (iommu->vgsts & DMA_GSTS_TES) {
			pkvm_err("iommu%d: TE allowed only once\n", iommu->seq_id);
			return -EBUSY;
		} else if (!(iommu->vgsts & DMA_GSTS_RTPS)) {
			pkvm_err("iommu%d: TE not allowed before SRTP\n", iommu->seq_id);
			return -EINVAL;
		}

		handle_gcmd_direct(iommu, DMA_GCMD_TE, true);
		pkvm_dbg("iommu%d: Translation enabled!\n", iommu->seq_id);
	} else {
		/*
		 * Translation is not really disabled as it would
		 * compromise pKVM security guarantees.
		 */
		iommu->vgsts &= ~DMA_GSTS_TES;
		pkvm_dbg("iommu%d: Translation marked as disabled!", iommu->seq_id);
	}

	return 0;
}

static int handle_global_cmd(struct intel_iommu *iommu, u32 val)
{
	u32 changed = (iommu->vgsts & DMAR_GSTS_EN_BITS) ^ val;

	if (!changed)
		return 0;

	if (hweight32(changed) > 1) {
		pkvm_warn("iommu%d: more than one changed bit in a gcmd write(%x)\n",
			  iommu->seq_id, val);
		return -EINVAL;
	}

	if (changed & ~DMAR_GCMD_SUPPORTED_BITS) {
		pkvm_warn("iommu%d: received GCMD for unsupported bit: %x\n",
			  iommu->seq_id, changed);
		return -EOPNOTSUPP;
	}

	pkvm_dbg("iommu%d: handle gcmd val 0x%x gsts 0x%x changed 0x%x\n",
		 iommu->seq_id, val, iommu->vgsts, changed);

	if (changed & DMA_GCMD_QIE)
		return handle_gcmd_qie(iommu, !!(val & changed));

	if (changed & DMA_GCMD_SRTP)
		return handle_gcmd_srtp(iommu);

	if (changed & DMA_GCMD_SIRTP)
		return handle_gcmd_sirtp(iommu);

	if (changed & DMA_GCMD_TE)
		return handle_gcmd_te(iommu, !!(val & changed));

	/*
	 * Check if the bits are allowed to be directly accessible by the host
	 * and passthrough if so.
	 */
	if (changed & ~DMAR_GCMD_DIRECT) {
		pkvm_warn("iommu%d: direct access of GCMD bit: %x(set=%d) not allowed\n",
			  iommu->seq_id, changed, !!(val & changed));
		return -EPERM;
	}

	return handle_gcmd_direct(iommu, changed, !!(val & changed));
}

int pkvm_iommu_mmio_read(u64 phys, int len, u64 *val)
{
	struct intel_iommu *iommu = iommu_from_phys(phys);
	unsigned long offset;
	int ret = 0;

	if (!iommu)
		return -EINVAL;

	pkvm_spin_lock(&iommu->lock);
	offset = phys - iommu->reg_phys;

	switch (offset) {
	case DMAR_GCMD_REG:
		ret = -EINVAL;
		break;
	case DMAR_CAP_REG:
		*val = iommu->cap;
		break;
	case DMAR_ECAP_REG:
		*val = iommu->ecap;
		break;
	case DMAR_IQA_REG:
		*val = iommu->viqa;
		break;
	case DMAR_RTADDR_REG:
		*val = iommu->vrta;
		break;
	case DMAR_IRTA_REG:
		*val = iommu->virta;
		break;
	case DMAR_GSTS_REG:
		*val = iommu->vgsts;
		break;
	default:
		/* Not emulated MMIO can directly go to hardware */
		ret = iommu_direct_mmio_read(iommu, phys, len, val);
	}

	pkvm_spin_unlock(&iommu->lock);
	return ret;
}

int pkvm_iommu_mmio_write(u64 phys, int len, u64 val)
{
	struct intel_iommu *iommu = iommu_from_phys(phys);
	unsigned long offset;
	int ret = 0;

	if (!iommu)
		return -EINVAL;

	pkvm_spin_lock(&iommu->lock);
	offset = phys - iommu->reg_phys;

	switch (offset) {
	case DMAR_CAP_REG:
		fallthrough;
	case DMAR_ECAP_REG:
		fallthrough;
	case DMAR_GSTS_REG:
		fallthrough;
	case DMAR_IQH_REG:
		ret = -EINVAL;
		break;
	case DMAR_GCMD_REG:
		ret = handle_global_cmd(iommu, val);
		break;
	case DMAR_IQA_REG:
		if (iommu->viqa) {
			pkvm_err("iommu%d: IQA set more than once!\n",
				 iommu->seq_id);
			ret = -EINVAL;
		} else {
			iommu->viqa = val;
		}
		break;
	case DMAR_IQT_REG:
		if (iommu->qi) {
			pkvm_err("iommu%d: write to IQT not allowed!\n",
				 iommu->seq_id);
			return -EPERM;
		}
		break;
	case DMAR_RTADDR_REG:
		if (sm_supported(iommu) && !(val & DMA_RTADDR_SMT)) {
			pkvm_err("iommu%d: SM enabled but not set in RTA!\n",
				 iommu->seq_id);
			ret = -EINVAL;
		} else if (iommu->vgsts & DMA_GSTS_TES) {
			pkvm_err("iommu%d: Setting RTA after Translation enabled!\n",
				 iommu->seq_id);
			ret = -EBUSY;
		} else {
			iommu->vrta = val;
		}
		break;
	case DMAR_IRTA_REG:
		if (iommu->ir_table) {
			pkvm_err("iommu%d: IRTA already set\n", iommu->seq_id);
			ret = -EBUSY;
		} else if (iommu->vgsts & DMA_GSTS_IRES) {
			pkvm_err("iommu%d: Setting IRTA after IRE enabled!\n",
				 iommu->seq_id);
			ret = -EBUSY;
		} else {
			iommu->virta = val;
		}
		break;
	case DMAR_FSTS_REG:
	case DMAR_FECTL_REG:
	case DMAR_FEDATA_REG:
	case DMAR_PERFINTRSTS_REG:
	case DMAR_PERFINTRCTL_REG:
	case DMAR_PERFINTRDATA_REG:
		/*
		 * Interrupt control registers that carry no memory-targeting
		 * risk; pass through directly:
		 *   FSTS/PERFINTRSTS   - RW1C status, driver clears fault bits
		 *   FECTL/PERFINTRCTL  - interrupt mask/unmask
		 *   FEDATA/PERFINTRDATA - MSI data payload
		 * The corresponding address registers (FEADDR/FEUADDR and
		 * PERFINTRADDR/PERFINTRUADDR) are handled separately with MSI
		 * address range and memory map validation.
		 */
		ret = iommu_direct_mmio_write(iommu, phys, len, val);
		break;
	case DMAR_ECMD_REG: {
		/*
		 * Extended Command Interface. Only allow performance monitoring
		 * counter control commands (ENABLE/DISABLE/FREEZE/UNFREEZE).
		 * These toggle counter state only and have no memory or DMA
		 * remapping implications. Block all other command codes as they
		 * are reserved or unknown and cannot be reasoned about safely.
		 */
		u8 ecmd = val & 0xff;

		if (ecmd != DMA_ECMD_ENABLE && ecmd != DMA_ECMD_DISABLE &&
		    ecmd != DMA_ECMD_FREEZE && ecmd != DMA_ECMD_UNFREEZE) {
			pkvm_err("iommu%d: unsupported ECMD 0x%x blocked\n",
				 iommu->seq_id, ecmd);
			ret = -EPERM;
		} else {
			ret = iommu_direct_mmio_write(iommu, phys, len, val);
		}
		break;
	}
	case DMAR_PMEN_REG:
		/*
		 * pKVM disables PMRs during iommu init. Allow the host to write
		 * PMEN with EPM clear (the safe state) but drop the writes
		 * Block any attempt to re-enable PMRs (EPM bit set) as that
		 * could disrupt guest DMA.
		 */
		if (val & DMA_PMEN_EPM) {
			pkvm_err("iommu%d: attempt to enable PMRs blocked\n",
				 iommu->seq_id);
			ret = -EPERM;
		}
		break;
	case DMAR_FEADDR_REG:
		/*
		 * FEADDR holds the MSI destination address for fault events.
		 * The IOMMU writes to this address when a fault occurs, bypassing
		 * DMA remapping. Ensure it targets only the x86 MSI address range
		 * (0xFEE00000) to prevent writes to protected memory.
		 *
		 * In x2APIC mode FEUADDR provides the upper 32 bits; check the
		 * combined 64-bit address against the system memory map to catch
		 * addresses like 0x100_FEE00000 (~1 TiB) that could target DRAM.
		 */
		if ((val & X86_MSI_ADDR_MASK) != X86_MSI_ADDR_BASE) {
			pkvm_err("iommu%d: FEADDR 0x%llx not in MSI address range\n",
				 iommu->seq_id, val);
			ret = -EPERM;
		} else {
			u32 feuaddr = readl(iommu->reg + DMAR_FEUADDR_REG);

			if (msi_addr_targets_memory((u32)val, feuaddr)) {
				pkvm_err("iommu%d: FEADDR/FEUADDR 0x%llx targets system memory\n",
					 iommu->seq_id, ((u64)feuaddr << 32) | (u32)val);
				ret = -EPERM;
			} else {
				ret = iommu_direct_mmio_write(iommu, phys, len, val);
			}
		}
		break;
	case DMAR_FEUADDR_REG:
		/*
		 * FEUADDR holds the upper 32 bits of the MSI address.
		 *
		 * In xAPIC mode all MSI addresses fit in 32 bits (0xFEE00000),
		 * so FEUADDR is zero.
		 *
		 * In x2APIC mode (VT-d spec §5.1.6.2, Figure 5-6), the IOMMU
		 * places APIC ID bits [31:8] in FEUADDR bits [31:8]; bits [7:0]
		 * are reserved and must be zero.  Reject writes that set the
		 * reserved low byte, but allow any value in bits [31:8] — those
		 * are a legitimate x2APIC destination ID.
		 *
		 * Also check that the combined 64-bit address with the current
		 * FEADDR does not fall in system memory.
		 */
		if (val & 0xFF) {
			pkvm_err("iommu%d: FEUADDR 0x%llx has reserved bits set\n",
				 iommu->seq_id, val);
			ret = -EPERM;
		} else {
			u32 feaddr = readl(iommu->reg + DMAR_FEADDR_REG);

			if (msi_addr_targets_memory(feaddr, (u32)val)) {
				pkvm_err("iommu%d: FEUADDR/FEADDR 0x%llx targets system memory\n",
					 iommu->seq_id, (val << 32) | feaddr);
				ret = -EPERM;
			} else {
				ret = iommu_direct_mmio_write(iommu, phys, len, val);
			}
		}
		break;
	case DMAR_PERFINTRADDR_REG:
		/*
		 * PERFINTRADDR holds the MSI destination address for performance
		 * monitoring interrupts. Same threat as FEADDR: the IOMMU writes
		 * to this address when a perf event fires, bypassing DMA remapping.
		 * Validate it targets only the x86 MSI address range (0xFEE00000)
		 * and that the combined 64-bit address with PERFINTRUADDR does not
		 * fall in system memory.
		 */
		if ((val & X86_MSI_ADDR_MASK) != X86_MSI_ADDR_BASE) {
			pkvm_err("iommu%d: PERFINTRADDR 0x%llx not in MSI address range\n",
				 iommu->seq_id, val);
			ret = -EPERM;
		} else {
			u32 perfintruaddr = readl(iommu->reg + DMAR_PERFINTRUADDR_REG);

			if (msi_addr_targets_memory((u32)val, perfintruaddr)) {
				pkvm_err("iommu%d: PERFINTRADDR/PERFINTRUADDR 0x%llx targets system memory\n",
					 iommu->seq_id,
					 ((u64)perfintruaddr << 32) | (u32)val);
				ret = -EPERM;
			} else {
				ret = iommu_direct_mmio_write(iommu, phys, len, val);
			}
		}
		break;
	case DMAR_PERFINTRUADDR_REG:
		/*
		 * PERFINTRUADDR holds the upper 32 bits of the perf event MSI
		 * address.  Same x2APIC layout as FEUADDR (§5.1.6.2): bits
		 * [31:8] carry APIC ID[31:8]; bits [7:0] are reserved.
		 *
		 * Also check that the combined 64-bit address with the current
		 * PERFINTRADDR does not fall in system memory.
		 */
		if (val & 0xFF) {
			pkvm_err("iommu%d: PERFINTRUADDR 0x%llx has reserved bits set\n",
				 iommu->seq_id, val);
			ret = -EPERM;
		} else {
			u32 perfintraddr = readl(iommu->reg + DMAR_PERFINTRADDR_REG);

			if (msi_addr_targets_memory(perfintraddr, (u32)val)) {
				pkvm_err("iommu%d: PERFINTRUADDR/PERFINTRADDR 0x%llx targets system memory\n",
					 iommu->seq_id, (val << 32) | perfintraddr);
				ret = -EPERM;
			} else {
				ret = iommu_direct_mmio_write(iommu, phys, len, val);
			}
		}
		break;
	default:
		/*
		 * Deny-by-default: block all registers not explicitly handled
		 * above.  Any register the host driver legitimately needs must
		 * be added as an explicit case; unknown or unreviewed registers
		 * must not reach hardware.
		 */
		pkvm_err("iommu%d: unrecognized register write blocked at offset 0x%lx val 0x%llx\n",
			 iommu->seq_id, offset, val);
		ret = -EPERM;
	}

	pkvm_spin_unlock(&iommu->lock);
	return ret;
}

int __init prepare_iommu(struct intel_iommu_info *info)
{
	struct intel_iommu *iommu;

	if (nr_iommus >= PKVM_MAX_IOMMU_NUM)
		return -ENOMEM;

	iommu = &iommus[nr_iommus++];
	iommu->reg_phys = info->reg_phys;
	iommu->reg_size = info->reg_size;
	iommu->cap = info->cap;
	iommu->ecap = info->ecap;
	iommu->agaw = info->agaw;
	iommu->msagaw = info->msagaw;
	iommu->seq_id = info->seq_id;

	iommu_paging_structure_coherent &= iommu_paging_structure_coherency(iommu);

	return 0;
}

/*
 * Validate that the MSI address programmed into a pair of 32-bit address
 * registers (lower @addr_reg, upper @uaddr_reg) does not target system
 * memory.  Called at deprivilege time to catch firmware/BIOS values that
 * were set before pKVM took control.
 */
static int iommu_validate_msi_addr(struct intel_iommu *iommu,
				   u32 addr_reg, u32 uaddr_reg,
				   const char *name)
{
	u32 addr  = readl(iommu->reg + addr_reg);
	u32 uaddr = readl(iommu->reg + uaddr_reg);

	if (msi_addr_targets_memory(addr, uaddr)) {
		pkvm_err("iommu%d: %s address 0x%llx targets system memory\n",
			 iommu->seq_id, name,
			 ((u64)uaddr << 32) | addr);
		return -EPERM;
	}
	return 0;
}

static int iommu_init(struct intel_iommu *iommu)
{
	int ret;

	if (!iommu->reg_phys)
		return -EFAULT;

	iommu->reg = __pkvm_va(iommu->reg_phys);
	ret = pkvm_hyp_mmu_map((unsigned long)iommu->reg, iommu->reg_phys,
			       iommu->reg_size, (u64)pgprot_val(PAGE_KERNEL_IO_NOCACHE));
	if (ret) {
		pkvm_err("iommu%d: failed to map MMIO space in hyp(err=%d)\n",
			 iommu->seq_id, ret);
		return ret;
	}

	ret = pkvm_host_donate_hyp_mmio(iommu->reg_phys, PAGE_ALIGN(iommu->reg_size));
	if (ret) {
		pkvm_err("iommu%d: failed to donate MMIO space to hyp(err=%d)\n",
			 iommu->seq_id, ret);
		return ret;
	}

	pkvm_spin_lock_init(&iommu->lock);

	/*
	 * Take a snapshot of GSTS. GCMD updates will be handled by pKVM and
	 * hence this snapshot will be kept up-to-date by pKVM and used as
	 * virtual GSTS for the host.
	 */
	iommu->vgsts = readl(iommu->reg + DMAR_GSTS_REG);

	/*
	 * Protected Memory Regions (PMRs) are a legacy DMA protection mechanism
	 * used by firmware before DMA remapping is active. Host driver disables
	 * PMRs once DMA remapping is set up. Disable them explicitly to be on
	 * the safer side in case host driver doesn't get to do it.
	 */
	if ((cap_plmr(iommu->cap) || cap_phmr(iommu->cap)) &&
	    (readl(iommu->reg + DMAR_PMEN_REG) & DMA_PMEN_EPM)) {
		u32 pmen;

		pkvm_dbg("iommu%d: disabling PMRs\n", iommu->seq_id);
		pmen = readl(iommu->reg + DMAR_PMEN_REG);
		pmen &= ~DMA_PMEN_EPM;
		writel(pmen, iommu->reg + DMAR_PMEN_REG);
	}

	/*
	 * IR setup (IRTA write + GCMD SIRTP) happens during x2apic enable,
	 * before pKVM deprivilege.  If the IR table pointer is already active,
	 * read the existing IRTA value and protect the table now.
	 */
	if (iommu->vgsts & DMA_GSTS_IRTPS) {
		iommu->virta = readq(iommu->reg + DMAR_IRTA_REG);
		ret = iommu_protect_ir_table(iommu);
		if (ret)
			return ret;
	}

	/*
	 * Validate that FEADDR/FEUADDR do not target system memory.  Firmware
	 * programs these before deprivilege; a buggy or malicious firmware
	 * could have pointed them at protected memory.  The check is possible
	 * here because pKVM's MMU (and thus the full memory map) is
	 * initialized before iommu_init().
	 */
	ret = iommu_validate_msi_addr(iommu, DMAR_FEADDR_REG, DMAR_FEUADDR_REG,
				      "fault event");
	if (ret)
		return ret;

	/*
	 * Same check for PERFINTRADDR/PERFINTRUADDR if performance monitoring
	 * is supported.
	 */
	if (ecap_pms(iommu->ecap)) {
		ret = iommu_validate_msi_addr(iommu, DMAR_PERFINTRADDR_REG,
					      DMAR_PERFINTRUADDR_REG,
					      "perf monitoring");
		if (ret)
			return ret;
	}

	return 0;
}

int pkvm_intel_iommu_init(void)
{
	int i;

	for (i = 0; i < nr_iommus; i++) {
		int ret = iommu_init(&iommus[i]);

		if (ret)
			return ret;
	}

	init_pt_domain();

	return 0;
}

void pkvm_iommu_pt_flush(unsigned long paddr, unsigned long size)
{
	if (pt_domain.qi_batch)
		cache_tag_flush_range(&pt_domain, paddr, paddr + size - 1, 0);
}
