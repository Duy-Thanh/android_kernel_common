/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2022 Intel Corporation
 */
#ifndef _ASM_X86_PKVM_H
#define _ASM_X86_PKVM_H

#include <asm/kvm_para.h>
#include <asm/io.h>
#include <asm/coco.h>

/* PKVM Hypercalls */
#define PKVM_HC_KVM_CALL		0
#define PKVM_HC_INIT_FINALISE		1
#define PKVM_HC_MMIO_ACCESS		7
#define PKVM_HC_ADD_PTDEV		10

#define PKVM_HC_ENABLE_IOMMU		11
#define PKVM_HC_DISABLE_IOMMU		12
#define PKVM_HC_IOMMU_CLEAR_CE		13
#define PKVM_HC_IOMMU_SET_LM_CE		14
#define PKVM_HC_IOMMU_SET_SM_CE		15
#define PKVM_HC_IOMMU_CLEAR_PASID_ENTRY	16
#define PKVM_HC_IOMMU_SET_PASID_FL	17
#define PKVM_HC_IOMMU_SET_PASID_SL	18
#define PKVM_HC_IOMMU_SET_SM_CE_PRE	19
#define PKVM_HC_IOMMU_DOMAIN_ALLOC	20
#define PKVM_HC_IOMMU_DOMAIN_FREE	21
#define PKVM_HC_IOMMU_MAP_PAGES		22
#define PKVM_HC_IOMMU_UNMAP_PAGES	23
#define PKVM_HC_IOMMU_CACHE_ASSIGN	24
#define PKVM_HC_IOMMU_CACHE_UNASSIGN	25
#define PKVM_HC_SUBMIT_QI		26

/*
 * Internal hypercall to commit the pkvm initialization
 * status to success or failure. This is to make internal
 * hypercalls to be unavailable for general use after
 * successful pkvm initialization and to rollback pkvm
 * initialization actions on failure.
 */
#define __PKVM_HC_COMMIT_FINALISE	100

/*
 * Internal hypercall to reprivilege cpus on pkvm
 * initialization failure.
 */
#define __PKVM_HC_REPRIVILEGE_VCPU	101

#ifdef CONFIG_PKVM_INTEL_PVIOMMU
#define PKVM_MAX_PASID_BITS	20
#else
/*
 * 15bits for PASID, DO NOT change it, based on it,
 * the size of PASID DIR table can kept as one page
 */
#define PKVM_MAX_PASID_BITS	15
#endif
#define PKVM_MAX_PASID		(1 << PKVM_MAX_PASID_BITS)

struct pkvm_iommu_driver {
	int (*prepare_driver)(void);
	int (*init_driver)(void);
};

#ifdef CONFIG_PKVM_INTEL

#define PKVM_MAX_IOMMU_PAGE_DONATION	64
/*
 * For managing IOMMU page tables, pkvm would need free pages and host
 * donates the pages as needed. This avoids static allocation of pages
 * in pkvm during boot. map and unmap hypercalls use this structure as
 * a two-way communication mechanism to manage page donation. Host
 * allocates pages and updates nr_donated for the map hypercall. pkvm
 * updates nr_returned with the pages not used or freed during map/unmap
 * hypercalls.
 */
struct pkvm_iommu_page_donation {
	int nr_pages;
	phys_addr_t pages[PKVM_MAX_IOMMU_PAGE_DONATION]; /* page gpa */
};

/*
 * Generic hypercall parameter for clearing legacy
 * and scalable mode context entries and pasid table
 * entries.
 */
struct pkvm_clear_translation_param {
	/*
	 * Input: bdf to indentify entry
	 *        to be cleared.
	 */
	u16 bdf;
	/*
	 * Input: pasid to indentify pasid table
	 *        entry to be cleared.
	 *        Ignored for context teardown.
	 */
	u32 pasid;
	/*
	 * Output: did of the cleared entry
	 *         Ignored for scalable mode
	 *         context entry.
	 */
	u16 did;
};

struct pkvm_lm_context_param {
	u16 bdf;
	u16 did;
	u16 domain_agaw;
	u64 domain_pgd_gpa;
	u64 context_gpa;
};

struct pkvm_sm_context_param {
	u16 bdf;
	u8 ats_supported:1;
	u8 pasid_supported:1;
	u32 max_pasid;
	u64 pasid_dir_gpa;
	u64 context_gpa;
};

struct pkvm_sm_context_pre_param {
	u16 bdf;
	u8 val;
	u16 did;
};

struct pkvm_pasid_table_param {
	u16 bdf;
	u16 did;
	u32 pasid;
	u8 force_snooping:1;
	u8 dirty_tracking:1;
	u32 max_pasid;
	u64 pasid_dir_gpa;
	u64 pasid_table_gpa;
	u16 domain_gaw;
	u16 domain_agaw;
	u64 domain_pgd_gpa;
};

struct pkvm_domain_param {
	u16 bdf;
	u16 gaw;
	u8 agaw;
	u8 iommu_superpage: 4;
	u8 iommu_coherency: 1;
	u8 use_first_level: 1;
	u64 max_addr;
	u64 pgd_gpa;
};

struct pkvm_cache_tag_param {
	u64 pgd_gpa;
	int type;
	u64 phys;
	u8 bus;
	u8 devfn;
	u16 pfsid;
	u8 ats_qdep;
	u8 dtlb_extra_inval;
	u16 domain_id;
	u32 pasid;
};

/*
 * parameters passed by host for MAP_PAGES hypercall.
 */
struct pkvm_iommu_map_param {
	u64 pgd_gpa;
	u64 iov_pfn;
	u64 phys_pfn;
	u64 nr_pages;
	u64 prot;
};

#ifndef __PKVM_HYP__

extern bool __read_mostly enable_pkvm;	/* kernel command-line flag */

extern struct static_key_false pkvm_enabled_key;

static inline bool pkvm_enabled(void)
{
	return static_branch_likely(&pkvm_enabled_key);
}

#ifdef CONFIG_PKVM_INTEL_PVIOMMU
static inline bool pkvm_pviommu_enabled(void)
{
	return pkvm_enabled();
}
#else
static inline bool pkvm_pviommu_enabled(void)
{
	return false;
}
#endif

int pkvm_iommu_register_driver(const struct pkvm_iommu_driver *kern_ops);

static inline u64 pkvm_readq(void __iomem *reg, unsigned long reg_phys,
			     unsigned long offset)
{
	if (pkvm_enabled())
		return (u64)kvm_hypercall3(PKVM_HC_MMIO_ACCESS, true,
					   sizeof(u64), reg_phys + offset);
	else
		return readq(reg + offset);
}

static inline u32 pkvm_readl(void __iomem *reg, unsigned long reg_phys,
			     unsigned long offset)
{
	if (pkvm_enabled())
		return (u32)kvm_hypercall3(PKVM_HC_MMIO_ACCESS, true,
					   sizeof(u32), reg_phys + offset);
	else
		return readl(reg + offset);
}

static inline void pkvm_writeq(void __iomem *reg, unsigned long reg_phys,
			       unsigned long offset, u64 val)
{
	if (pkvm_enabled())
		kvm_hypercall4(PKVM_HC_MMIO_ACCESS, false, sizeof(u64),
			       reg_phys + offset, val);
	else
		writeq(val, reg + offset);
}

static inline void pkvm_writel(void __iomem *reg, unsigned long reg_phys,
			       unsigned long offset, u32 val)
{
	if (pkvm_enabled())
		kvm_hypercall4(PKVM_HC_MMIO_ACCESS, false, sizeof(u32),
			       reg_phys + offset, (u64)val);
	else
		writel(val, reg + offset);
}

static inline long pkvm_hc_enable_iommu(unsigned long reg_phys,
		unsigned long root_gpa)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_ENABLE_IOMMU,
				reg_phys, root_gpa);

	return ret;
}

static inline long pkvm_hc_disable_iommu(unsigned long reg_phys)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall1(PKVM_HC_DISABLE_IOMMU, reg_phys);

	return ret;
}

static inline long pkvm_hc_iommu_clear_ce(unsigned long reg_phys,
		struct pkvm_clear_translation_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_CLEAR_CE,
				reg_phys, virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_lm_ce(unsigned long reg_phys,
		struct pkvm_lm_context_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_LM_CE, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_sm_ce(unsigned long reg_phys,
		struct pkvm_sm_context_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_SM_CE, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_sm_ce_pre(unsigned long reg_phys,
		struct pkvm_sm_context_pre_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_SM_CE_PRE, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_clear_pasid_entry(unsigned long reg_phys,
		struct pkvm_clear_translation_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_CLEAR_PASID_ENTRY, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_pasid_fl(unsigned long reg_phys,
		struct pkvm_pasid_table_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_PASID_FL, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_set_pasid_sl(unsigned long reg_phys,
		struct pkvm_pasid_table_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_SET_PASID_SL, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_domain_alloc(unsigned long reg_phys,
		struct pkvm_domain_param *param)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall2(PKVM_HC_IOMMU_DOMAIN_ALLOC, reg_phys,
				virt_to_phys(param));

	return ret;
}

static inline long pkvm_hc_iommu_domain_free(u64 pgd_gpa)
{
	long ret = 0;

	if (pkvm_pviommu_enabled())
		ret = kvm_hypercall1(PKVM_HC_IOMMU_DOMAIN_FREE, pgd_gpa);

	return ret;
}

static inline long pkvm_hc_iommu_map_pages(struct pkvm_iommu_map_param *param,
		struct pkvm_iommu_page_donation *donation)
{
	long ret = 0;
	if (pkvm_pviommu_enabled()) {
		ret = kvm_hypercall2(PKVM_HC_IOMMU_MAP_PAGES,
				virt_to_phys(param), virt_to_phys(donation));
	}

	return ret;
}

static inline long pkvm_hc_iommu_unmap_pages(unsigned long pgd_gpa, unsigned long start_pfn,
		unsigned long last_pfn, struct pkvm_iommu_page_donation *donation)
{
	long ret = 0;
	if (pkvm_pviommu_enabled()) {
		ret = kvm_hypercall4(PKVM_HC_IOMMU_UNMAP_PAGES, pgd_gpa, start_pfn, last_pfn,
				virt_to_phys(donation));
	}

	return ret;
}

static inline long pkvm_hc_cache_tag_assign(unsigned long pgd_gpa)
{
	long ret = 0;
	if (pkvm_pviommu_enabled()) {
		ret = kvm_hypercall1(PKVM_HC_IOMMU_CACHE_ASSIGN, pgd_gpa);
	}

	return ret;
}

static inline long pkvm_hc_cache_tag_unassign(unsigned long pgd_gpa)
{
	long ret = 0;
	if (pkvm_enabled()) {
		ret = kvm_hypercall1(PKVM_HC_IOMMU_CACHE_UNASSIGN, pgd_gpa);
	}

	return ret;
}

static inline int pkvm_hc_qi_submit_sync(unsigned long reg_phys, unsigned long desc,
		unsigned int count)
{
	if (pkvm_pviommu_enabled())
		kvm_hypercall3(PKVM_HC_SUBMIT_QI, reg_phys, desc, count);

	return 0;
}
#else /* __PKVM_HYP__ */

/* we are in pkvm hypervisor, pkvm is enabled by definition */
#define enable_pkvm true

#endif /* __PKVM_HYP__ */

static inline void pkvm_update_iommu_virtual_caps(u64 *cap, u64 *ecap)
{
#ifndef __PKVM_HYP__
	if (!enable_pkvm)
		return;
#endif

	if (cap)
		/*
		 * Set caching mode as linux OS will runs in a VM
		 * with controlling a virtual IOMMU device emulated
		 * by pkvm.
		 */
		*cap |= 1 << 7;

	if (ecap) {
		u64 tmp;

		/*
		 * Some IOMMU capabilities cannot be directly used by the linux
		 * IOMMU driver after the linux is deprivileged, which is because after
		 * deprivileging, pkvm IOMMU driver will control the physical IOMMU and
		 * it is designed to use physical IOMMU in two ways for better performance
		 * and simpler implementation:
		 * 1. using nested translation with the first-level from the deprivileged
		 * linux IOMMU driver and EPT as second-level.
		 * 2. using second-level only translation with EPT.
		 * The linux IOMMU driver then uses an virtual IOMMU device emulated by
		 * pkvm IOMMU driver.
		 *
		 * Way#1 and way#2 can only support the linux IOMMU driver works in
		 * first-level translation mode or HW pass-through mode. To guarantee
		 * this, let linux IOMMU driver to pick up the supported capabilities
		 * when running at the bare metal if pkvm is enabled, to make it as a
		 * pkvm-awared IOMMU kernel driver.
		 *
		 * So disable SLTS and Nest.
		 */
		*ecap &= ~((1UL << 46) | (1UL << 26));

		/* limit PASID to reduce the memory consumptions */
		tmp = min_t(u64, (PKVM_MAX_PASID_BITS - 1),
			    (*ecap & GENMASK_ULL(39, 35)) >> 35);
		*ecap = (*ecap & ~GENMASK_ULL(39, 35)) | (tmp << 35);
	}
}
#else /* CONFIG_PKVM_INTEL */

#define enable_pkvm false

static inline bool pkvm_enabled(void)
{
	return false;
}

static inline bool pkvm_pviommu_enabled(void)
{
	return false;
}

static inline int pkvm_iommu_register_driver(const struct pkvm_iommu_driver *kern_ops)
{
	return -EPERM;
}

#endif /* CONFIG_PKVM_INTEL */

#endif
