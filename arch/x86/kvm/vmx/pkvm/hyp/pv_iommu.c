/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2025 Google
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
#include "iommu_internal.h"
#include "debug.h"
#include "ptdev.h"
#include "mem_protect.h"
#include "iommu_spgt.h"
#include "bug.h"
#include "iommu.h"
#include "iommu_domain.h"

/*
 * Taken from linux/pci.h
 */
#define PCI_DEVID(bus, devfn)   ((((u16)(bus)) << 8) | (devfn))
#define PCI_BUS_NUM(x) (((x) >> 8) & 0xff)
#define PCI_DEV_FN(x) ((x) & 0xff)

int initialize_iommu_pgt(struct pkvm_iommu *iommu)
{
	/*
	 * This is a nop as pv iommu implementation do not use
	 * iommu->pgt except for iommu-pgt.root_pa and that is
	 * initialized elsewhere.
	 */
	return 0;
}

unsigned long pkvm_iommu_set_rta(unsigned long phys, unsigned long rta_phys)
{
	struct pkvm_iommu *iommu = find_iommu_by_reg_phys(phys);
	struct viommu_reg *vreg = &iommu->viommu.vreg;
	unsigned long old_rta_phys = iommu->pgt.root_pa;

	pkvm_spin_lock(&iommu->lock);
	vreg->gsts &= ~DMA_GSTS_RTPS;

	pkvm_dbg("pkvm: %s: rta_phys: %lx\n", __func__, rta_phys);

	/*
	 * Host has the pages identity mapped. So not converting from host gpa to hpa.
	 */
	iommu->pgt.root_pa = rta_phys;
	vreg->rta = rta_phys;

	if (!iommu->activated) {
		if (activate_iommu(iommu)) {
			pkvm_dbg("pkvm: %s: iommu%d failed to activate\n", __func__, iommu->iommu.seq_id);
		}
	} else if (vreg->gsts & DMA_GSTS_TES) {
		flush_context_cache(iommu, 0, 0, 0, DMA_CCMD_GLOBAL_INVL);
		flush_iotlb(iommu, 0, 0, 0, DMA_TLB_GLOBAL_FLUSH);
	}

	vreg->gsts |= DMA_GSTS_RTPS;
	pkvm_spin_unlock(&iommu->lock);

	/*
	 * Remove the mapping for the root table page so that host
	 * will not be able to directly access it.
	 */
	if (old_rta_phys != rta_phys) {
		if (old_rta_phys) {
			pkvm_dbg("pkvm: %s: mapping prev root_table page[0x%lx] back to host\n",
					__func__, old_rta_phys);
			__pkvm_hyp_donate_host(old_rta_phys, PAGE_SIZE);
		}
		pkvm_dbg("pkvm: %s: unmapping root_table page[0x%lx] from host\n",
				__func__, rta_phys);
		__pkvm_host_donate_hyp(rta_phys, PAGE_SIZE);
	}

	root_tbl_walk(iommu);
	return 0;
}

static inline bool __valid_paging_pgd(u64 pgd)
{
	return pgd && (pgd != pkvm_host_ept_pgd());
}

unsigned long pkvm_iommu_update_ce(struct kvm_vcpu *hvcpu, unsigned long param_gva, unsigned long donation_gva)
{
	struct pkvm_iommu_page_donation donation;
	struct pkvm_update_ce_param param;
	struct context_entry *context;
	struct root_entry *root_entry;
	struct context_entry *ce;
	struct pkvm_iommu *iommu;
	struct root_entry *root;
	struct x86_exception e;
	u64 old_ce_pgd, new_ce_pgd;
	unsigned long ret;
	u8 bus, devfn;
	u64 old_rte, new_rte;
	u16 did;

	ret = read_gva(hvcpu, param_gva, &param, sizeof(struct pkvm_update_ce_param), &e);
	if (ret < 0) {
		pkvm_err("pkvm: %s Failed to read update_ce_param(gva: %lx from host!\n",
				__func__, param_gva);
		return ret;
	}

	ret = read_gva(hvcpu, donation_gva, &donation, sizeof(struct pkvm_iommu_page_donation), &e);
	if (ret < 0) {
		pkvm_err("pkvm: %s Failed to read donation(gva: %lx from host!\n",
				__func__, donation_gva);
		return ret;
	}

	iommu = find_iommu_by_reg_phys(param.reg_phys);
	PKVM_ASSERT(iommu);

	root_entry = pkvm_phys_to_virt(iommu->pgt.root_pa);
	context = pkvm_phys_to_virt(param.rte & VTD_PAGE_MASK);
	bus = PCI_BUS_NUM(param.bdf);
	devfn = PCI_DEV_FN(param.bdf);

	pkvm_spin_lock(&iommu->lock);

	root = &root_entry[bus];

	if (ecap_smts(iommu->iommu.ecap)) {
		if (devfn >= 0x80)
			ce = &context[2 * (devfn - 0x80)];
		else
			ce = &context[2 * devfn];
	}
	else {
		ce = &context[devfn];
		old_ce_pgd = ce->lo & VTD_PAGE_MASK;
		new_ce_pgd = param.ce_lo & VTD_PAGE_MASK;
	}

	if (ecap_smts(iommu->iommu.ecap) && devfn >= 0x80)
		old_rte = root->hi & VTD_PAGE_MASK;
	else
		old_rte = root->lo & VTD_PAGE_MASK;

	/*
	 * Remove the mapping for the context table page so that host
	 * will not be able to directly access it.
	 */
	new_rte = param.rte & VTD_PAGE_MASK;
	if (old_rte != new_rte) {
		if (old_rte) {
			pkvm_dbg("pkvm: %s: mapping prev context_table page[0x%llx] back to host\n",
					__func__, old_rte);
			__pkvm_hyp_donate_host(old_rte, PAGE_SIZE);
		}
		if (new_rte) {
			pkvm_dbg("pkvm: %s: unmapping context_table page[0x%llx] from host\n",
				__func__, new_rte);
			__pkvm_host_donate_hyp_noflush(new_rte, PAGE_SIZE);
		}
	}

	if (ecap_smts(iommu->iommu.ecap))
		pkvm_dbg("pkvm: %s: old_rte=%llx, new_rte: %llx, old_ce: (%llx:%llx), new_ce: (%llx:%llx)\n",
				__func__, old_rte, new_rte, ce->hi, ce->lo, param.ce_hi, param.ce_lo);
	else {
		did = context_domain_id(ce);
		pkvm_dbg("pkvm: %s: did: %d, old_rte=%llx, new_rte: %llx, old_ce: (%llx:%llx), new_ce: (%llx:%llx)\n",
				__func__, did, old_rte, new_rte, ce->hi, ce->lo, param.ce_hi, param.ce_lo);
	}

	/*
	 * Set the context and root entries.
	 */
	if (ecap_smts(iommu->iommu.ecap) && devfn >= 0x80)
		root->hi = param.rte;
	else
		root->lo = param.rte;

	ce->hi = param.ce_hi;
	ce->lo = param.ce_lo;

	/*
	 * Always set translation type to MULTI_LEVEL to ensure address
	 * translation and to disable device TLB for security.
	 */
	if (!ecap_smts(iommu->iommu.ecap) && context_lm_get_tt(ce) == CONTEXT_TT_PASS_THROUGH) {
		unsigned long pgd = pkvm_host_ept_pgd();
		int level = pkvm_host_ept_level();
		u8 aw;

		context_lm_set_tt(ce, CONTEXT_TT_MULTI_LEVEL);
		context_lm_set_slptr(ce, pgd);
		aw = (level == 3) ? 1 :
		     (level == 4) ? 2 : 3;
		context_lm_set_aw(ce, aw);
		new_ce_pgd = pgd;
	}

	if (!ecap_smts(iommu->iommu.ecap) && old_ce_pgd != new_ce_pgd) {
		struct pkvm_iommu_domain *domain;
		struct pkvm_ptdev *ptdev;
		if (__valid_paging_pgd(old_ce_pgd)) {
			domain = pkvm_get_iommu_domain(old_ce_pgd);
			PKVM_ASSERT(domain);
			pkvm_dbg("pkvm: %s put iommu domain pgd: %llx\n", __func__, domain->pgd);
			ptdev = iommu_find_ptdev(iommu, param.bdf, 0);
			PKVM_ASSERT(ptdev);
			iommu_del_ptdev(iommu, ptdev);
			pkvm_domain_detach_iommu(domain, iommu);
			pkvm_put_iommu_domain(domain);
		}
		if (__valid_paging_pgd(new_ce_pgd)) {
			domain = pkvm_get_iommu_domain(new_ce_pgd);
			PKVM_ASSERT(domain);

			new_ce_pgd = pkvm_domain_update_pgd(domain,
					&donation, param.domain_agaw);
			if (new_ce_pgd != domain->pgd) {
				pkvm_dbg("pkvm: %s, domain changed pgd [%llx] => [%llx]\n",
					__func__, domain->pgd, new_ce_pgd);
				pkvm_put_iommu_domain(domain);
				domain = pkvm_alloc_iommu_domain(new_ce_pgd);
				param.pgd = new_ce_pgd;
				context_lm_set_slptr(ce, new_ce_pgd);
				domain = pkvm_get_iommu_domain(new_ce_pgd);
			}

			domain->iommu_coherency = param.iommu_coherency;
			domain->iommu_superpage = param.iommu_superpage;
			domain->gaw = param.domain_gaw;
			domain->agaw = param.domain_agaw;

			pkvm_domain_attach_iommu(domain, iommu, did);
			PKVM_ASSERT(!iommu_find_ptdev(iommu, param.bdf, 0));
			ptdev = iommu_add_ptdev(iommu, param.bdf, 0);
			pkvm_setup_ptdev_did(ptdev, did);
			pkvm_dbg("pkvm: %s get iommu domain pgd: %llx\n", __func__, domain->pgd);
			pkvm_put_iommu_domain(domain);
		}
	}

	/*
	 * TODO: Revisit the cache flushing and optimize for cases like present to non-present
	 * and non-present-to-present.
	 */
	if (!iommu_coherency(iommu->iommu.ecap)) {
		pkvm_clflush_cache_range(root, sizeof(*root));
		pkvm_clflush_cache_range(ce, sizeof(*ce));
	}

	flush_context_cache(iommu, 0, param.bdf, DMA_CCMD_MASK_NOBIT, DMA_CCMD_DEVICE_INVL);

	if (ecap_smts(iommu->iommu.ecap))
		flush_iotlb(iommu, 0, 0, 0, DMA_TLB_DSI_FLUSH);
	else
		flush_iotlb(iommu, did, 0, 0, DMA_TLB_DSI_FLUSH);

	pkvm_spin_unlock(&iommu->lock);

	ret = write_gva(hvcpu, donation_gva, &donation, sizeof(struct pkvm_iommu_page_donation), &e);
	if (ret < 0) {
		pkvm_err("pkvm: %s Failed to write donation (gva: %lx) tp host!\n",
				__func__, donation_gva);
		return ret;
	}

	ret = write_gva(hvcpu, param_gva, &param, sizeof(struct pkvm_update_ce_param), &e);
	if (ret < 0) {
		pkvm_err("pkvm: %s Failed to write param (gva: %lx) tp host!\n",
				__func__, param_gva);
		return ret;
	}

	return ret;
}
