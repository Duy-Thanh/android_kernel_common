/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2022 Intel Corporation
 */
#include <../drivers/iommu/intel/iommu.h>
#include <asm/pkvm_spinlock.h>
#include <pkvm.h>
#include "pkvm_hyp.h"
#include "gfp.h"
#include "memory.h"
#include "mmu.h"
#include "ept.h"
#include "pgtable.h"
#include "mem_protect.h"
#include "iommu_internal.h"
#include "debug.h"
#include "ptdev.h"
#include "iommu_spgt.h"
#include "bug.h"
#include "iommu.h"

#define for_each_valid_iommu(p)						\
	for ((p) = iommus; (p) < iommus + PKVM_MAX_IOMMU_NUM; (p)++)	\
		if (!(p) || !(p)->iommu.reg_phys) {			\
			continue;					\
		} else

struct pkvm_iommu iommus[PKVM_MAX_IOMMU_NUM];

static struct hyp_pool iommu_pool;

void *iommu_zalloc_pages(size_t size)
{
	return hyp_alloc_pages(&iommu_pool, get_order(size));
}

void *iommu_zalloc_page(void)
{
	return hyp_alloc_pages(&iommu_pool, 0);
}

void iommu_get_page(void *vaddr)
{
	hyp_get_page(&iommu_pool, vaddr);
}

void iommu_put_page(void *vaddr)
{
	hyp_put_page(&iommu_pool, vaddr);
}

void iommu_flush_cache(void *ptep, unsigned int size)
{
	pkvm_clflush_cache_range(ptep, size);
}

struct ptdev_info *iommu_find_ptdev(struct pkvm_iommu *iommu, u16 bdf, u32 pasid)
{
	struct ptdev_info *p;

	list_for_each_entry(p, &iommu->ptdev_head, iommu_node) {
		struct pkvm_ptdev *ptdev = p->ptdev;
		if (match_ptdev(ptdev, bdf) && match_ptdev_info(p, pasid))
			return p;
	}

	return NULL;
}

struct ptdev_info *iommu_add_ptdev(struct pkvm_iommu *iommu, u16 bdf, u32 pasid)
{
	struct ptdev_info *ptdev_info = iommu_find_ptdev(iommu, bdf, pasid);

	/*
	 * ptdev was already added.
	 */
	if (ptdev_info)
		return ptdev_info;

	ptdev_info = pkvm_alloc_ptdev_info(bdf, pasid, iommu_coherency(&iommu->iommu));
	if (!ptdev_info)
		return NULL;

	list_add_tail(&ptdev_info->iommu_node, &iommu->ptdev_head);
	return ptdev_info;
}

void iommu_del_ptdev(struct pkvm_iommu *iommu, struct ptdev_info *ptdev_info)
{
	list_del_init(&ptdev_info->iommu_node);
	pkvm_put_ptdev_info(ptdev_info);
}

void iommu_del_ptdevs(struct pkvm_iommu *iommu, u16 bdf)
{
	struct ptdev_info *p, *tmp;

	list_for_each_entry_safe(p, tmp, &iommu->ptdev_head, iommu_node) {
		struct pkvm_ptdev *ptdev = p->ptdev;
		if (match_ptdev(ptdev, bdf))
			iommu_del_ptdev(iommu, p);
	}
}

int iommu_audit_did(struct pkvm_iommu *iommu, u16 did, int shadow_vm_handle)
{
	struct ptdev_info *tmp;
	int ret = 0;

	list_for_each_entry(tmp, &iommu->ptdev_head, iommu_node) {
		if (tmp->shadow_vm_handle != shadow_vm_handle) {
			if (tmp->did == did) {
				/*
				 * The devices belong to different VMs but behind
				 * the same IOMMU, cannot use the same did.
				 */
				ret = -EPERM;
				break;
			}
		}
	}

	return ret;
}

bool is_dev_in_satc(u16 bdf)
{
	for (int idx = 0; idx < pkvm_hyp->satc_dev_cnt; idx++)
		if (bdf == pkvm_hyp->satc_dev_bdf[idx])
			return true;

	return false;
}

static void initialize_viommu_reg(struct pkvm_iommu *iommu, u32 gsts)
{
	struct viommu_reg *vreg = &iommu->viommu.vreg;

	vreg->cap = iommu->iommu.cap;
	vreg->ecap = iommu->iommu.ecap;
	pkvm_update_iommu_virtual_caps(&vreg->cap, &vreg->ecap);

	vreg->gsts = gsts;

	pkvm_dbg("%s: iommu phys reg 0x%llx cap 0x%llx ecap 0x%llx gsts 0x%x\n",
		 __func__, iommu->iommu.reg_phys, vreg->cap, vreg->ecap, vreg->gsts);

	/* rta updated when host writes to DMAR_RTADDR_REG */

	/* Invalidate Queue regs are updated when create descriptor */
}

/*
 * Following code is copied from drivers/iommu/intel/iommu.c
 * Temporariliy parking here until we find a long term solution to sharing code
 * between host driver and pkvm
 */

// START: duplicated code from drivers/iommu/intel/iommu.c

/*
 * Calculate the Supported Adjusted Guest Address Widths of an IOMMU.
 * Refer to 11.4.2 of the VT-d spec for the encoding of each bit of
 * the returned SAGAW.
 */
static unsigned long __iommu_calculate_sagaw(struct intel_iommu *iommu)
{
	unsigned long fl_sagaw, sl_sagaw;

	fl_sagaw = BIT(2) | (cap_fl5lp_support(iommu->cap) ? BIT(3) : 0);
	sl_sagaw = cap_sagaw(iommu->cap);

	/* Second level only. */
	if (!pkvm_sm_supported(iommu) || !ecap_flts(iommu->ecap))
		return sl_sagaw;

	/* First level only. */
	if (!ecap_slts(iommu->ecap))
		return fl_sagaw;

	return fl_sagaw & sl_sagaw;
}

static int __iommu_calculate_agaw(struct intel_iommu *iommu, int max_gaw)
{
	unsigned long sagaw;
	int agaw;

	sagaw = __iommu_calculate_sagaw(iommu);
	for (agaw = width_to_agaw(max_gaw); agaw >= 0; agaw--) {
		if (test_bit(agaw, &sagaw))
			break;
	}

	return agaw;
}

/*
 * Calculate max SAGAW for each iommu.
 */
int iommu_calculate_max_sagaw(struct intel_iommu *iommu)
{
	return __iommu_calculate_agaw(iommu, MAX_AGAW_WIDTH);
}

/*
 * calculate agaw for each iommu.
 * "SAGAW" may be different across iommus, use a default agaw, and
 * get a supported less agaw for iommus that don't support the default agaw.
 */
int iommu_calculate_agaw(struct intel_iommu *iommu)
{
	return __iommu_calculate_agaw(iommu, DEFAULT_DOMAIN_ADDRESS_WIDTH);
}
// END: duplicated code from drivers/iommu/intel/iommu.c

static int initialize_qi(struct pkvm_iommu *iommu);

int pkvm_init_iommu(unsigned long mem_base, unsigned long nr_pages)
{
	struct pkvm_iommu_info *info = &pkvm_hyp->iommu_infos[0];
	struct pkvm_iommu *piommu = &iommus[0];
	int i, ret = hyp_pool_init(&iommu_pool, mem_base >> PAGE_SHIFT, nr_pages, 0);

	if (ret)
		return ret;

	for (i = 0; i < PKVM_MAX_IOMMU_NUM; piommu++, info++, i++) {
		u32 gsts;

		if (!info->reg_phys)
			break;

		INIT_LIST_HEAD(&piommu->ptdev_head);

		pkvm_spin_lock_init(&piommu->lock);
		piommu->iommu.reg_phys = info->reg_phys;
		piommu->iommu.reg_size = info->reg_size;
		piommu->iommu.reg = pkvm_iophys_to_virt(info->reg_phys);
		if ((unsigned long)piommu->iommu.reg == INVALID_ADDR)
			return -ENOMEM;
		piommu->iommu.seq_id = i;

		ret = pkvm_mmu_map((unsigned long)piommu->iommu.reg,
				   (unsigned long)info->reg_phys,
				   info->reg_size, 1 << PG_LEVEL_4K,
				   PKVM_PAGE_IO_NOCACHE);
		if (ret)
			return ret;

		piommu->iommu.cap = readq(piommu->iommu.reg + DMAR_CAP_REG);
		piommu->iommu.ecap = readq(piommu->iommu.reg + DMAR_ECAP_REG);
		piommu->iommu.agaw = iommu_calculate_agaw(&piommu->iommu);
		if (piommu->iommu.agaw < 0) {
			pkvm_err("Cannot get a valid agaw for iommu (seq_id = %d)\n", piommu->iommu.seq_id);
			return -EFAULT;
		}

		piommu->iommu.msagaw = iommu_calculate_max_sagaw(&piommu->iommu);
		if (piommu->iommu.msagaw < 0) {
			pkvm_err("Cannot get a valid max agaw for iommu (seq_id = %d)\n", piommu->iommu.seq_id);
			return -EFAULT;
		}

		gsts = readl(piommu->iommu.reg + DMAR_GSTS_REG);
		/* cache the enabled features from Global Status register */
		piommu->iommu.gcmd = gsts & DMAR_GSTS_EN_BITS;

		initialize_viommu_reg(piommu, gsts);
		/*
		 * Initialize QI if it was enabled before pkvm activation.
		 */
		if (gsts & DMA_GSTS_QIES) {
			ret = initialize_qi(piommu);
			if (ret)
				return ret;
		}

	}

	return 0;
}

static void restore_qi(struct pkvm_iommu *iommu, u64 iqa)
{
	u32 sts;

	sts = readl(iommu->iommu.reg + DMAR_GSTS_REG);
	if (!(sts & DMA_GSTS_QIES))
		return;

	/* Disable QI */
	iommu->iommu.gcmd &= ~DMA_GCMD_QIE;
	writel(iommu->iommu.gcmd, iommu->iommu.reg + DMAR_GCMD_REG);
	PKVM_IOMMU_WAIT_OP(iommu->iommu.reg + DMAR_GSTS_REG,
			   readl, !(sts & DMA_GSTS_QIES), sts);

	/* Set tail to 0 */
	writel(0, iommu->iommu.reg + DMAR_IQT_REG);

	/* Set IQA */
	writeq(iqa, iommu->iommu.reg + DMAR_IQA_REG);

	/* Enable QI */
	iommu->iommu.gcmd |= DMA_GCMD_QIE;
	writel(iommu->iommu.gcmd, iommu->iommu.reg + DMAR_GCMD_REG);
	PKVM_IOMMU_WAIT_OP(iommu->iommu.reg + DMAR_GSTS_REG,
			   readl, (sts & DMA_GSTS_QIES), sts);
}

void pkvm_undo_iommu(void)
{
	struct pkvm_iommu *iommu;

	for_each_valid_iommu(iommu) {
		u64 prot = pkvm_mkstate(HOST_EPT_DEF_MMIO_PROT, PKVM_PAGE_OWNED);

		if (!iommu->activated)
			continue;
		if (pkvm_host_ept_map((unsigned long)iommu->iommu.reg_phys,
			     (unsigned long)iommu->iommu.reg_phys,
			     iommu->iommu.reg_size, 0, prot)) {
			pkvm_err("pkvm: failed to map back IOMMU mmio space[%llx:%llx] to host!\n",
					(unsigned long long)iommu->iommu.reg_phys,
					iommu->iommu.reg_size);
		}

		restore_qi(iommu, iommu->viommu.vreg.iqa);
	}
}

static void enable_qi(struct pkvm_iommu *iommu)
{
	void *desc = iommu->qi.desc;
	int dw, qs;
	u32 sts;

	dw = !!ecap_smts(iommu->iommu.ecap);
	qs = fls(iommu->qi.free_cnt >> (7 + !dw)) - 1;

	/* Disable QI */
	sts = readl(iommu->iommu.reg + DMAR_GSTS_REG);
	if (sts & DMA_GSTS_QIES) {
		iommu->iommu.gcmd &= ~DMA_GCMD_QIE;
		writel(iommu->iommu.gcmd, iommu->iommu.reg + DMAR_GCMD_REG);
		PKVM_IOMMU_WAIT_OP(iommu->iommu.reg + DMAR_GSTS_REG,
				   readl, !(sts & DMA_GSTS_QIES), sts);
	}

	/* Set tail to 0 */
	writel(0, iommu->iommu.reg + DMAR_IQT_REG);

	/* Set IQA */
	iommu->piommu_iqa = pkvm_virt_to_phys(desc) | (dw << 11) | qs;
	writeq(iommu->piommu_iqa, iommu->iommu.reg + DMAR_IQA_REG);

	/* Enable QI */
	iommu->iommu.gcmd |= DMA_GCMD_QIE;
	writel(iommu->iommu.gcmd, iommu->iommu.reg + DMAR_GCMD_REG);
	PKVM_IOMMU_WAIT_OP(iommu->iommu.reg + DMAR_GSTS_REG,
			   readl, (sts & DMA_GSTS_QIES), sts);
}

static int initialize_qi(struct pkvm_iommu *iommu)
{
	struct pkvm_viommu *viommu = &iommu->viommu;
	struct q_inval *qi = &iommu->qi;
	void __iomem *reg = iommu->iommu.reg;

	if (iommu->qi_enabled) {
		pkvm_dbg("pkvm: QI already enabled!\n");
		return 0;
	}

	pkvm_spin_lock_init(&iommu->qi_lock);
	/*
	 * Before switching the descriptor, need to wait any pending
	 * invalidation descriptor completed. According to spec 6.5.2,
	 * The invalidation queue is considered quiesced when the queue
	 * is empty (head and tail registers equal) and the last
	 * descriptor completed is an Invalidation Wait Descriptor
	 * (which indicates no invalidation requests are pending in hardware).
	 */
	while (readq(reg + DMAR_IQH_REG) !=
		readq(reg + DMAR_IQT_REG))
		cpu_relax();

	viommu->vreg.iqa = viommu->iqa = readq(reg + DMAR_IQA_REG);
	viommu->vreg.iq_head = readq(reg + DMAR_IQH_REG);
	viommu->vreg.iq_tail = readq(reg + DMAR_IQT_REG);

	if (viommu->vreg.gsts & DMA_GSTS_QIES) {
		struct qi_desc *wait_desc;
		u64 iqa = viommu->iqa;
		int shift = IQ_DESC_SHIFT(iqa);
		int offset = ((viommu->vreg.iq_head >> shift) +
			      IQ_DESC_LEN(iqa) - 1) % IQ_DESC_LEN(iqa);
		int *desc_status;

		/* Find out the last descriptor */
		wait_desc = pkvm_phys_to_virt(IQ_DESC_BASE_PHYS(iqa)) + (offset << shift);

		pkvm_dbg("pkvm: viommu iqa 0x%llx head 0x%llx tail 0x%llx qw0 0x%llx qw1 0x%llx",
				viommu->vreg.iqa, viommu->vreg.iq_head, viommu->vreg.iq_tail,
				wait_desc->qw0, wait_desc->qw1);

		/*
		 * If there were invalidation events in flight, wait until
		 * hardware acknowledges the events. Linux IOMMU driver guarantees
		 * that last descriptor will be a invalidation wait descriptor.
		 * So wait on the last descriptor if its a valid descriptor.
		 * The invalidation queue ring buffer containing descriptors will be
		 * zero initialized by the host iommu driver. So we need to check
		 * if the descriptor is valid only if the descriptor contents are
		 * non-zero.
		 */
		if (wait_desc->qw0) {
			if (QI_DESC_TYPE(wait_desc->qw0) != QI_IWD_TYPE) {
				pkvm_err("pkvm: %s: expect wait desc but 0x%llx\n",
					 __func__, wait_desc->qw0);
				return -EINVAL;
			}

			desc_status = pkvm_phys_to_virt(wait_desc->qw1);
			/*
			 * Wait until the wait descriptor is completed.
			 *
			 * The desc_status is from host. Checking this in pkvm
			 * is relying on host IOMMU driver won't release the
			 * desc_status after it is completed, and this is guarantee
			 * by the current Linux IOMMU driver.
			 */
			while (READ_ONCE(*desc_status) == QI_IN_USE)
				cpu_relax();
		}
	}

	qi->free_cnt = PKVM_QI_DESC_ALIGNED_SIZE / sizeof(struct qi_desc);
#ifdef CONFIG_PKVM_INTEL_PVIOMMU
	{
		int order = ecap_smts(iommu->iommu.ecap) ? 2 : 1;
		u64 desc_phys = IQ_DESC_BASE_PHYS(iommu->viommu.iqa);
		if (pkvm_switch_host_ept_ro(desc_phys, (1 << order) * VTD_PAGE_SIZE)) {
			pkvm_err("pkvm: %s: failed to write protect QI desc!\n", __func__);
			return -EFAULT;
		}
		qi->desc = pkvm_phys_to_virt(desc_phys);
		qi->desc_status = pkvm_phys_to_virt(desc_phys + order * VTD_PAGE_SIZE);
	}
#else
	qi->desc = iommu_zalloc_pages(PKVM_QI_DESC_ALIGNED_SIZE);
	if (!qi->desc)
		return -ENOMEM;

	qi->desc_status = iommu_zalloc_pages(PKVM_QI_DESC_STATUS_ALIGNED_SIZE);
	if (!qi->desc_status) {
		iommu_put_page(qi->desc);
		return -ENOMEM;
	}
#endif

	enable_qi(iommu);
	iommu->qi_enabled = true;
	return 0;
}

static int qi_check_fault(struct pkvm_iommu *iommu, int wait_index)
{
	u32 fault;
	struct q_inval *qi = &iommu->qi;

	if (qi->desc_status[wait_index] == QI_ABORT)
		return -EAGAIN;

	fault = readl(iommu->iommu.reg + DMAR_FSTS_REG);

	/*
	 * If IQE happens, the head points to the descriptor associated
	 * with the error. No new descriptors are fetched until the IQE
	 * is cleared.
	 */
	if (fault & DMA_FSTS_IQE) {
		writel(DMA_FSTS_IQE, iommu->iommu.reg + DMAR_FSTS_REG);
		pkvm_dbg("pkvm: Invalidation Queue Error (IQE) cleared\n");
	}

	/*
	 * If ITE happens, all pending wait_desc commands are aborted.
	 * No new descriptors are fetched until the ITE is cleared.
	 */
	if (fault & DMA_FSTS_ITE) {
		writel(DMA_FSTS_ITE, iommu->iommu.reg + DMAR_FSTS_REG);
		pkvm_dbg("pkvm: Invalidation Time-out Error (ITE) cleared\n");
	}

	if (fault & DMA_FSTS_ICE) {
		writel(DMA_FSTS_ICE, iommu->iommu.reg + DMAR_FSTS_REG);
		pkvm_dbg("pkvm: Invalidation Completion Error (ICE) cleared\n");
	}

	return 0;
}

static void __submit_qi(struct pkvm_iommu *iommu, struct qi_desc *base, int count)
{
	int len = IQ_DESC_LEN(iommu->piommu_iqa), i, wait_index;
	int shift = IQ_DESC_SHIFT(iommu->piommu_iqa);
	struct q_inval *qi = &iommu->qi;
	struct qi_desc *to, *from;
	int required_cnt = count + 2;
	void *desc = qi->desc;
	int *desc_status, rc;

	pkvm_spin_lock(&iommu->qi_lock);
	/*
	 * Detect if the free descriptor count is enough or not
	 */
	while (qi->free_cnt < required_cnt) {
		u64 head = readq(iommu->iommu.reg + DMAR_IQH_REG) >> shift;
		int busy_cnt = (READ_ONCE(qi->free_head) + len - head) % len;
		int free_cnt = len - busy_cnt;

		if (free_cnt >= required_cnt) {
			qi->free_cnt = free_cnt;
			break;
		}
		pkvm_spin_unlock(&iommu->qi_lock);
		cpu_relax();
		pkvm_spin_lock(&iommu->qi_lock);
	}

	for (i = 0; i < count; i++) {
		from = base + i;
		to = qi->desc + (((qi->free_head + i) % len) << shift);
		to->qw0 = from->qw0;
		to->qw1 = from->qw1;
	}

	wait_index = (qi->free_head + count) % len;
	/* setup wait descriptor */
	to = desc + (wait_index << shift);
	to->qw0 = QI_IWD_STATUS_DATA(QI_DONE) |
		  QI_IWD_STATUS_WRITE | QI_IWD_TYPE;

	desc_status = &qi->desc_status[wait_index];
	WRITE_ONCE(*desc_status, QI_IN_USE);
	to->qw1 = pkvm_virt_to_phys(desc_status);

	/* submit to hardware with wait descriptor */
	qi->free_cnt -= count + 1;
	qi->free_head = (qi->free_head + count + 1) % len;
	writel(qi->free_head << shift, iommu->iommu.reg + DMAR_IQT_REG);

	while (READ_ONCE(*desc_status) != QI_DONE) {
		rc = qi_check_fault(iommu, wait_index);
		if (rc)
			break;
		pkvm_spin_unlock(&iommu->qi_lock);
		cpu_relax();
		pkvm_spin_lock(&iommu->qi_lock);
	}

	if (*desc_status != QI_DONE)
		pkvm_err("pkvm: %s: failed with status %d\n",
			 __func__, *desc_status);

	/* release the free_cnt */
	qi->free_cnt += count + 1;

	pkvm_spin_unlock(&iommu->qi_lock);
}

void submit_qi(struct pkvm_iommu *iommu, struct qi_desc *base, int count)
{
	int max_len = IQ_DESC_LEN(iommu->piommu_iqa) - 2;
	int submit_count;

	do {
		submit_count = count > max_len ? max_len : count;
		__submit_qi(iommu, base, submit_count);

		count -= submit_count;
		base += submit_count;
	} while (count > 0);
}

void flush_context_cache(struct pkvm_iommu *iommu, u16 did,
				u16 sid, u8 fm, u64 type)
{
	struct qi_desc desc = {.qw1 = 0, .qw2 = 0, .qw3 = 0};

	desc.qw0 = QI_CC_FM(fm) | QI_CC_SID(sid) | QI_CC_DID(did) |
		   QI_CC_GRAN(type) | QI_CC_TYPE;

	submit_qi(iommu, &desc, 1);
}

void flush_pasid_cache(struct pkvm_iommu *iommu, u16 did,
			      u64 granu, u32 pasid)
{
	struct qi_desc desc = {.qw1 = 0, .qw2 = 0, .qw3 = 0};

	desc.qw0 = QI_PC_PASID(pasid) | QI_PC_DID(did) |
		   QI_PC_GRAN(granu) | QI_PC_TYPE;

	submit_qi(iommu, &desc, 1);
}

void flush_write_buffer(struct pkvm_iommu *iommu)
{
	void __iomem *reg = iommu->iommu.reg;
	u32 val;

	if (!cap_rwbf(iommu->iommu.cap))
		return;

	writel(iommu->iommu.gcmd | DMA_GCMD_WBF, reg + DMAR_GCMD_REG);

	PKVM_IOMMU_WAIT_OP(reg + DMAR_GSTS_REG,
		      readl, (!(val & DMA_GSTS_WBFS)), val);

}

void flush_iotlb(struct pkvm_iommu *iommu, u16 did, u64 addr,
			unsigned int size_order, u64 type)
{
	struct qi_desc desc;

	setup_iotlb_qi_desc(iommu, &desc, did, addr, size_order, type);
	submit_qi(iommu, &desc, 1);
}

void flush_piotlb(struct pkvm_iommu *iommu, u16 did, u32 pasid, u64 addr,
		     unsigned long npages, bool ih)
{
	struct qi_desc desc;

	qi_desc_piotlb(did, pasid, addr, npages, ih, &desc);
	submit_qi(iommu, &desc, 1);
}

static void set_root_table(struct pkvm_iommu *iommu)
{
	u64 val = iommu->pgt.root_pa;
	void __iomem *reg = iommu->iommu.reg;
	u32 sts;

	flush_write_buffer(iommu);

	/* Set scalable mode */
	if (ecap_smts(iommu->iommu.ecap))
		val |= DMA_RTADDR_SMT;

	writeq(val, reg + DMAR_RTADDR_REG);

	/*
	 * The shadow root table provides identical remapping results comparing
	 * with the previous guest root table, so it is allowed to switch if
	 * Translation Enable Status is still 1 according to IOMMU spec 6.6:
	 *
	 *  "
	 *  If software sets the root-table pointer while remapping hardware is
	 *  active (TES=1 in Global Status register), software must ensure the
	 *  structures referenced by the new root-table pointer provide identical
	 *  remapping results as the structures referenced by the previous root-table
	 *  pointer so that inflight requests are properly translated.
	 *  "
	 *
	 *  So don't need to turn off TE first before switching.
	 */
	writel(iommu->iommu.gcmd | DMA_GCMD_SRTP, reg + DMAR_GCMD_REG);

	PKVM_IOMMU_WAIT_OP(reg + DMAR_GSTS_REG, readl, (sts & DMA_GSTS_RTPS), sts);

	flush_context_cache(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL);
	if (ecap_smts(iommu->iommu.ecap))
		flush_pasid_cache(iommu, 0, QI_PC_GLOBAL, 0);
	flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);
	iommu->viommu.vreg.gsts |= DMA_GSTS_RTPS;
}

int enable_translation(struct pkvm_iommu *iommu)
{
	unsigned long vaddr = 0, vaddr_end = MAX_NUM_OF_ADDRESS_SPACE(iommu);
	void __iomem *reg = iommu->iommu.reg;
	u32 sts;
	int ret;

	if (iommu->iommu.gcmd & DMA_GCMD_TE) {
		pkvm_err("pkvm: %s: iommu%d already enabled!\n", __func__, iommu->iommu.seq_id);
		return -EINVAL;
	}

	ret = initialize_iommu_pgt(iommu);
	if (ret)
		return ret;

	set_root_table(iommu);

	ret = pkvm_host_ept_unmap((unsigned long)iommu->iommu.reg_phys,
			     (unsigned long)iommu->iommu.reg_phys,
			     iommu->iommu.reg_size);
	if (ret)
		return ret;

	iommu->viommu.vreg.gsts |= DMA_GSTS_TES;
	/*
	 * Sync shadow id table to emulate Translation enable.
	 */
	ret = sync_shadow_id(iommu, vaddr, vaddr_end, 0);
	if (ret) {
		pkvm_err("pkvm: %s: shadow sync for iommu%d failed!\n", __func__, iommu->iommu.seq_id);
		return ret;
	}

	iommu->iommu.gcmd |= DMA_GCMD_TE;

	writel(iommu->iommu.gcmd, reg + DMAR_GCMD_REG);

	PKVM_IOMMU_WAIT_OP(reg + DMAR_GSTS_REG, readl, (sts & DMA_GSTS_TES), sts);

	iommu->activated = true;
	pkvm_dbg("pkvm: %s: iommu%d activated\n", __func__, iommu->iommu.seq_id);
	root_tbl_walk(iommu);
	return ret;
}

void disable_translation(struct pkvm_iommu *iommu)
{
	unsigned long vaddr = 0, vaddr_end = MAX_NUM_OF_ADDRESS_SPACE(iommu);

	/*
	 * Free shadow to emulate Translation disable.
	 *
	 * Don't disable translation as we still
	 * need to protect against the device.
	 */
	free_shadow_id(iommu, vaddr, vaddr_end);
	iommu->viommu.vreg.gsts &= ~DMA_GSTS_TES;
}

#ifndef CONFIG_PKVM_INTEL_PVIOMMU
static void handle_gcmd_te(struct pkvm_iommu *iommu, bool en)
{
	if (en) {
		if (enable_translation(iommu))
			return;
	} else {
		disable_translation(iommu);
	}

	pkvm_dbg("pkvm: %s: %s TE\n", __func__, en ? "enable" : "disable");

	flush_context_cache(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL);
	if (ecap_smts(iommu->iommu.ecap))
		flush_pasid_cache(iommu, 0, QI_PC_GLOBAL, 0);
	flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);

	root_tbl_walk(iommu);
}
#endif

#ifndef CONFIG_PKVM_INTEL_PVIOMMU
static void handle_gcmd_srtp(struct pkvm_iommu *iommu)
{
	struct viommu_reg *vreg = &iommu->viommu.vreg;

	if (vreg->gsts & DMA_GSTS_RTPS) {
		pkvm_err("pkvm: %s: set SRTP not allowed more than once!\n", __func__);
		return;
	}

	/*
	 * We don't do anything specific other than setting the bit in GSTS.
	 * RTADDR_REG(vreg->rta) will be used to initialize viommu pagetable
	 * later in initialize_iommu_pgt.
	 */
	pkvm_dbg("pkvm: %s: set SRTP val 0x%llx\n", __func__, vreg->rta);

	vreg->gsts |= DMA_GSTS_RTPS;
}
#endif

static void handle_gcmd_qie(struct pkvm_iommu *iommu, bool en)
{
	struct viommu_reg *vreg = &iommu->viommu.vreg;

	if (en) {
		if (vreg->iq_tail != 0) {
			pkvm_err("pkvm: Queue invalidation descriptor tail is not zero\n");
			return;
		}

		/* Update the iqa from vreg */
		iommu->viommu.iqa = vreg->iqa;
		vreg->iq_head = 0;
		initialize_qi(iommu);
		vreg->gsts |= DMA_GSTS_QIES;
		pkvm_dbg("pkvm: %s: enabled QI\n", __func__);
		return;
	}

	if (vreg->iq_head != vreg->iq_tail) {
		pkvm_err("pkvm: Queue invalidation descriptor is not empty yet\n");
		return;
	}

	vreg->iq_head = 0;
	vreg->gsts &= ~DMA_GSTS_QIES;
	pkvm_dbg("pkvm: %s: disabled QI\n", __func__);
}

static void handle_gcmd_direct(struct pkvm_iommu *iommu, u32 val)
{
	struct viommu_reg *vreg = &iommu->viommu.vreg;
	unsigned long changed = ((vreg->gsts ^ val) & DMAR_GCMD_DIRECT) &
				DMAR_GSTS_EN_BITS;
	unsigned long set = (val & DMAR_GCMD_DIRECT) & ~DMAR_GSTS_EN_BITS;
	u32 cmd, gcmd, sts;
	int bit;

	if ((changed | set) & DMAR_GCMD_PROTECTED) {
		pkvm_dbg("pkvm:%s touching protected bits changed 0x%lx set 0x%lx\n",
			 __func__, changed, set);
		return;
	}

	if (changed) {
		pkvm_dbg("pkvm: %s: changed 0x%lx\n", __func__, changed);
		gcmd = READ_ONCE(iommu->iommu.gcmd);
		for_each_set_bit(bit, &changed, BITS_PER_BYTE * sizeof(vreg->gsts)) {
			cmd = 1 << bit;
			if (val & cmd) {
				/* enable */
				gcmd |= cmd;
				writel(gcmd, iommu->iommu.reg + DMAR_GCMD_REG);
				PKVM_IOMMU_WAIT_OP(iommu->iommu.reg + DMAR_GSTS_REG,
						   readl, (sts & cmd), sts);
				vreg->gsts |= cmd;
				pkvm_dbg("pkvm: %s: enable cmd bit %d\n", __func__, bit);
			} else {
				/* disable */
				gcmd &= ~cmd;
				writel(gcmd, iommu->iommu.reg + DMAR_GCMD_REG);
				PKVM_IOMMU_WAIT_OP(iommu->iommu.reg + DMAR_GSTS_REG,
						   readl, !(sts & cmd), sts);
				vreg->gsts &= ~cmd;
				pkvm_dbg("pkvm: %s: disable cmd bit %d\n", __func__, bit);
			}
		}
		WRITE_ONCE(iommu->iommu.gcmd, gcmd);
	}

	if (set) {
		pkvm_dbg("pkvm: %s: set 0x%lx\n", __func__, set);
		gcmd = READ_ONCE(iommu->iommu.gcmd);
		for_each_set_bit(bit, &set, BITS_PER_BYTE * sizeof(vreg->gsts)) {
			cmd = 1 << bit;
			vreg->gsts &= ~cmd;
			writel(gcmd | cmd, iommu->iommu.reg + DMAR_GCMD_REG);
			PKVM_IOMMU_WAIT_OP(iommu->iommu.reg + DMAR_GSTS_REG,
					   readl, (sts & cmd), sts);
			vreg->gsts |= cmd;
			pkvm_dbg("pkvm: %s: set cmd bit %d\n", __func__, bit);
		}
	}
}

static void handle_global_cmd(struct pkvm_iommu *iommu, u32 val)
{
	u32 changed = iommu->viommu.vreg.gsts ^ val;

	pkvm_dbg("pkvm: iommu%d: handle gcmd val 0x%x gsts 0x%x changed 0x%x\n",
		  iommu->iommu.seq_id, val, iommu->viommu.vreg.gsts, changed);

	if (changed & DMA_GCMD_TE) {
#ifdef CONFIG_PKVM_INTEL_PVIOMMU
		pkvm_err("pkvm: iommu%d: DMA_GCMD_TE supported only via hypercall!\n", iommu->iommu.seq_id);
#else
		handle_gcmd_te(iommu, !!(val & DMA_GCMD_TE));
#endif
	}

	if (val & DMA_GCMD_SRTP) {
#ifdef CONFIG_PKVM_INTEL_PVIOMMU
		pkvm_err("pkvm: iommu%d: DMA_GCMD_SRTP supported only via hypercall!\n", iommu->iommu.seq_id);
#else
		handle_gcmd_srtp(iommu);
#endif
	}

	if (changed & DMA_GCMD_QIE)
		handle_gcmd_qie(iommu, !!(val & DMA_GCMD_QIE));

	handle_gcmd_direct(iommu, val);
}

struct pkvm_iommu *find_iommu_by_reg_phys(unsigned long phys)
{
	struct pkvm_iommu *iommu;

	for_each_valid_iommu(iommu) {
		if ((phys >= iommu->iommu.reg_phys) &&
			(phys < (iommu->iommu.reg_phys + iommu->iommu.reg_size)))
			return iommu;
	}

	return NULL;
}

static unsigned long direct_access_iommu_mmio(struct pkvm_iommu *iommu,
					      bool is_read, int len,
					      unsigned long phys,
					      unsigned long val)
{
	unsigned long offset = phys - iommu->iommu.reg_phys;
	void *reg = iommu->iommu.reg + offset;
	unsigned long ret = 0;

	switch (len) {
	case 4:
		if (is_read)
			ret = (unsigned long)readl(reg);
		else
			writel((u32)val, reg);
		break;
	case 8:
		if (is_read)
			ret = (unsigned long)readq(reg);
		else
			writeq((u64)val, reg);
		break;
	default:
		pkvm_err("%s: %s: unsupported len %d\n", __func__,
			 is_read ? "read" : "write", len);
		break;
	}

	return ret;
}

static unsigned long access_iommu_mmio(struct pkvm_iommu *iommu, bool is_read,
				       int len, unsigned long phys,
				       unsigned long val)
{
	struct pkvm_viommu *viommu = &iommu->viommu;
	unsigned long offset = phys - iommu->iommu.reg_phys;
	unsigned long ret = 0;

	/* Only need to emulate part of the MMIO */
	switch (offset) {
	case DMAR_CAP_REG:
		if (is_read)
			ret = viommu->vreg.cap;
		break;
	case DMAR_ECAP_REG:
		if (is_read)
			ret = viommu->vreg.ecap;
		break;
	case DMAR_GCMD_REG:
		if (is_read)
			ret = 0;
		else
			handle_global_cmd(iommu, val);
		break;
	case DMAR_GSTS_REG:
		if (is_read)
			ret = viommu->vreg.gsts;
		break;
	case DMAR_RTADDR_REG:
		if (is_read)
			ret = viommu->vreg.rta;
		else
			viommu->vreg.rta = val;
		break;
	case DMAR_IQA_REG:
		if (is_read)
			ret = viommu->vreg.iqa;
		else
			viommu->vreg.iqa = val;
		break;
	case DMAR_IQH_REG:
		if (is_read)
			ret = viommu->vreg.iq_head;
		break;
	case DMAR_IQT_REG:
		if (is_read)
			ret = viommu->vreg.iq_tail;
		else {
			if (iommu->qi_enabled)
				ret = handle_qi_invalidation(iommu, val);
			else
				viommu->vreg.iq_tail = val;
		}
		break;
	default:
		/* Not emulated MMIO can directly goes to hardware */
		ret = direct_access_iommu_mmio(iommu, is_read, len, phys, val);
		break;
	}

	return ret;
}

unsigned long pkvm_access_iommu(bool is_read, int len, unsigned long phys, unsigned long val)
{
	struct pkvm_iommu *pkvm_iommu = find_iommu_by_reg_phys(phys);
	unsigned long ret;

	if (!pkvm_iommu) {
		pkvm_err("%s: cannot find pkvm iommu for reg 0x%lx\n",
			__func__, phys);
		return 0;
	}

	pkvm_spin_lock(&pkvm_iommu->lock);
	ret = access_iommu_mmio(pkvm_iommu, is_read, len, phys, val);
	pkvm_spin_unlock(&pkvm_iommu->lock);

	return ret;
}

bool is_mem_range_overlap_iommu(unsigned long start, unsigned long end)
{
	struct pkvm_iommu *iommu;

	for_each_valid_iommu(iommu) {
		if (end < iommu->iommu.reg_phys ||
			start > (iommu->iommu.reg_phys + iommu->iommu.reg_size - 1))
			continue;

		return true;
	}

	return false;
}

/*
 * TODO:
 * Currently assume that the bdf/pasid has ever been synced
 * so that the IOMMU can be found. If not synced, then cannot
 * get a valid IOMMU by calling this function.
 *
 * To handle this case, pKVM IOMMU driver needs to check the
 * DMAR to know which IOMMU should be used for this bdf/pasid.
 */
static struct pkvm_iommu *bdf_to_iommu(u16 bdf)
{
	struct pkvm_iommu *iommu, *find = NULL;
	struct ptdev_info *p;

	for_each_valid_iommu(iommu) {
		pkvm_spin_lock(&iommu->lock);
		list_for_each_entry(p, &iommu->ptdev_head, iommu_node) {
			struct pkvm_ptdev *ptdev = p->ptdev;
			if (match_ptdev(ptdev, bdf)) {
				find = iommu;
				break;
			}
		}
		pkvm_spin_unlock(&iommu->lock);
		if (find)
			break;
	}

	return find;
}

/*
 * pkvm_iommu_sync() - Sync IOMMU context/pasid entry according to a ptdev
 *
 * @bdf/pasid:		The corresponding IOMMU page table entry needs to sync.
 */
int pkvm_iommu_sync(u16 bdf, u32 pasid)
{
	struct pkvm_iommu *iommu = bdf_to_iommu(bdf);
	unsigned long id_addr, id_addr_end;
	struct ptdev_info *ptdev_info;
	u16 old_did;
	int ret;

	if (!iommu)
		return -ENODEV;

	ptdev_info = pkvm_get_ptdev_info(bdf, pasid);
	if (!ptdev_info)
		return -ENODEV;

	old_did = ptdev_info->did;

	if (ecap_smts(iommu->iommu.ecap)) {
		id_addr = ((unsigned long)bdf << DEVFN_SHIFT) |
			  ((unsigned long)pasid & ((1UL << MAX_NR_PASID_BITS) - 1));
		id_addr_end = id_addr + 1;
	} else {
		id_addr = (unsigned long)bdf << LM_DEVFN_SHIFT;
		id_addr_end = ((unsigned long)bdf + 1) << LM_DEVFN_SHIFT;
	}

	pkvm_spin_lock(&iommu->lock);
	ret = sync_shadow_id(iommu, id_addr, id_addr_end, 0);
	if (!ret) {
		if (old_did != ptdev_info->did) {
			/* Flush pasid cache and IOTLB for the valid old_did */
			if (ecap_smts(iommu->iommu.ecap))
				flush_pasid_cache(iommu, old_did, QI_PC_PASID_SEL, pasid);
			else
				flush_context_cache(iommu, old_did, 0, 0, DMA_CCMD_DOMAIN_INVL);
			flush_iotlb(iommu, old_did, 0, 0, DMA_TLB_DSI_FLUSH);
		}

		/* Flush pasid cache and IOTLB to make sure no stale TLB for the new did */
		if (ecap_smts(iommu->iommu.ecap))
			flush_pasid_cache(iommu, ptdev_info->did, QI_PC_PASID_SEL, pasid);
		else
			flush_context_cache(iommu, ptdev_info->did, 0, 0, DMA_CCMD_DOMAIN_INVL);
		flush_iotlb(iommu, ptdev_info->did, 0, 0, DMA_TLB_DSI_FLUSH);
	}
	pkvm_spin_unlock(&iommu->lock);

	pkvm_put_ptdev_info(ptdev_info);
	return ret;
}

bool pkvm_iommu_coherency(u16 bdf)
{
	struct pkvm_iommu *iommu = bdf_to_iommu(bdf);

	/*
	 * If cannot find a valid IOMMU by bdf/pasid, return
	 * false to present noncoherent, so that can guarantee
	 * the coherency through flushing cache by pkvm itself.
	 */
	if (!iommu)
		return false;

	return iommu_coherency(&iommu->iommu);
}

struct iotlb_flush_data {
	unsigned long desired_root_pa;
	unsigned long addr;
	int size_order;
	struct qi_desc *desc;
	int desc_max_index;
};

static void iommu_flush_iotlb(struct pkvm_iommu *iommu, struct iotlb_flush_data *data)
{
	struct ptdev_info *ptdev_info;
	struct qi_desc *desc = data->desc;
	int qi_desc_index = 0;

	pkvm_spin_lock(&iommu->lock);

	/* No need to flush IOTLB if there is no device on this IOMMU */
	if (list_empty(&iommu->ptdev_head))
		goto out;

	/*
	 * If the descriptor buffer is NULL, pKVM has to submit the QI
	 * request one by one which may be slow if there are a lot of
	 * devices connected to this IOMMU unit. So in this case, choose
	 * to submit one single global flush request to flush the IOTLB
	 * for all the devices.
	 */
	if (!desc) {
		flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);
		goto out;
	}

	/* Flush per domain */
	list_for_each_entry(ptdev_info, &iommu->ptdev_head, iommu_node) {
		struct qi_desc *tmp = desc;
		bool did_exist = false;
		int i;

		if (!ptdev_info->pgt || ptdev_info->pgt->root_pa != data->desired_root_pa)
			continue;

		for (i = 0; i < qi_desc_index; i++, tmp++) {
			/* The same did is already in descriptor page */
			if (ptdev_info->did == QI_DESC_IOTLB_DID(tmp->qw0)) {
				did_exist = true;
				break;
			}
		}

		if (did_exist)
			continue;
		/*
		 * Setup the page-selective or domain-selective qi descriptor
		 * based on IOMMU capability, and submit to HW when qi descriptor
		 * number reaches to the maximum count.
		 */
		if (cap_pgsel_inv(iommu->iommu.cap) &&
		    data->size_order <= cap_max_amask_val(iommu->iommu.cap))
			setup_iotlb_qi_desc(iommu, desc + qi_desc_index++,
					    ptdev_info->did, data->addr, data->size_order,
					    DMA_TLB_PSI_FLUSH);
		else
			setup_iotlb_qi_desc(iommu, desc + qi_desc_index++,
					    ptdev_info->did, 0, 0,
					    DMA_TLB_DSI_FLUSH);

		if (qi_desc_index == data->desc_max_index) {
			submit_qi(iommu, desc, qi_desc_index);
			qi_desc_index = 0;
		}
	}

	if (qi_desc_index)
		submit_qi(iommu, desc, qi_desc_index);
out:
	pkvm_spin_unlock(&iommu->lock);
}

void pkvm_iommu_flush_iotlb(struct pkvm_pgtable *pgt, unsigned long addr, unsigned long size)
{
	int size_order = ilog2(__roundup_pow_of_two(size >> VTD_PAGE_SHIFT));
	struct iotlb_flush_data data = {
		.desired_root_pa = pgt->root_pa,
		.addr = ALIGN_DOWN(addr, (1ULL << (VTD_PAGE_SHIFT + size_order))),
		.size_order = size_order,
	};
	struct pkvm_iommu *iommu;

	data.desc = iommu_zalloc_pages(PKVM_QI_DESC_ALIGNED_SIZE);
	if (data.desc)
		/* Reserve space for one wait desc and one desc between head and tail */
		data.desc_max_index = PKVM_QI_DESC_ALIGNED_SIZE / sizeof(struct qi_desc) - 2;

	for_each_valid_iommu(iommu)
		iommu_flush_iotlb(iommu, &data);

	if (data.desc)
		iommu_put_page(data.desc);
}

void pkvm_iommu_submit_qi(unsigned long phys, unsigned long desc_gpa, int count)
{
	struct qi_desc *desc = host_gpa2hva(desc_gpa);
	struct pkvm_iommu *pkvm_iommu = find_iommu_by_reg_phys(phys);

	submit_qi(pkvm_iommu, desc, count);
}
