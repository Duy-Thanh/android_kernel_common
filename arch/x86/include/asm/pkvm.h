/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (C) 2022 Intel Corporation
 */
#ifndef _ASM_X86_PKVM_H
#define _ASM_X86_PKVM_H

#include <asm/kvm_para.h>
#include <asm/io.h>
#include <asm/coco.h>

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

#define TO_PKVM_HC(f)		CONCATENATE(__pkvm__, f)

enum pkvm_hc {
	#define PKVM_HC(f)	TO_PKVM_HC(f),
	#include <asm/pkvm_hypercalls.h>

	MAX_PKVM_HYPERCALLS
};

static inline unsigned long __pkvm_hypercall(unsigned long nr, unsigned long p1,
					     unsigned long p2, unsigned long p3,
					     unsigned long p4, unsigned long p5)
{
	unsigned long ret;

	asm volatile(KVM_HYPERCALL
		     : "=a"(ret)
		     : "a"(nr), "b"(p1), "c"(p2), "d"(p3), "S"(p4), "D"(p5)
		     : "memory");
	return ret;
}

#define __pkvm_hypercall_0(f)	__pkvm_hypercall(f, 0, 0, 0, 0, 0)

#define __pkvm_hypercall_1(f, a1)							\
	({										\
		__pkvm_hypercall(f,							\
			(unsigned long)(a1), 0, 0, 0, 0);				\
	})

#define __pkvm_hypercall_2(f, a1, a2)							\
	({										\
		__pkvm_hypercall(f,							\
			(unsigned long)(a1), (unsigned long)(a2), 0, 0, 0);		\
	})

#define __pkvm_hypercall_3(f, a1, a2, a3)						\
	({										\
		__pkvm_hypercall(f,							\
			(unsigned long)(a1), (unsigned long)(a2),			\
			(unsigned long)(a3), 0, 0);					\
	})

#define __pkvm_hypercall_4(f, a1, a2, a3, a4)						\
	({										\
		__pkvm_hypercall(f,							\
			(unsigned long)(a1), (unsigned long)(a2),			\
			(unsigned long)(a3), (unsigned long)(a4), 0);			\
	})

#define __pkvm_hypercall_5(f, a1, a2, a3, a4, a5)					\
	({										\
		__pkvm_hypercall(f,							\
			(unsigned long)(a1), (unsigned long)(a2),			\
			(unsigned long)(a3), (unsigned long)(a4),			\
			(unsigned long)(a5));						\
	})

#define pkvm_hypercall(f, ...)								\
	({										\
		CONCATENATE(__pkvm_hypercall_,						\
			    COUNT_ARGS(__VA_ARGS__))(TO_PKVM_HC(f), ##__VA_ARGS__);	\
	})

#ifdef CONFIG_PKVM_INTEL

/* pviommu Hypercall structures */
/*
 * With pviommu, iommu driver do not have full control over the iommu hardware and
 * access is mostly through hypercalls. Following are structure definitions for
 * passing information via the pviommu hypercalls.
 *
 * Most structures will have a `u64 phys` which denotes the base physical address
 * of iommu's mmio region. pkvm uses this to identify the iommu.
 */

/*
 * For managing IOMMU page tables, pkvm would need free pages and host
 * donates the pages as needed. This avoids static allocation of pages
 * in pkvm during boot. map and unmap hypercalls use this structure as
 * a two-way communication mechanism to manage page donation. Host
 * allocates pages and updates nr_pages for the map hypercall. pkvm
 * updates nr_pages with the pages not used or freed during map/unmap
 * hypercalls.
 */
union pkvm_iommu_page_donation {
	struct {
		/*
		 * Number of pages available.
		 * Updated by host after filling pages and updated by pkvm when
		 * pages are consumed or filled back. pkvm fills pages back when
		 * pagetable is freed or pages combined to build super pages.
		 */
		u64 nr_pages;
		DECLARE_FLEX_ARRAY(phys_addr_t, pages);
	};
	u8 __padding[PAGE_SIZE];
} __aligned(PAGE_SIZE);

#define PKVM_MAX_IOMMU_PAGE_DONATION	\
	((sizeof(union pkvm_iommu_page_donation) - offsetof(union pkvm_iommu_page_donation, pages)) / sizeof(phys_addr_t))

/*
 * Maximum pages that could be donated by host.
 * pkvm_iommu_page_donation can hold more, but reserving rest of the
 * spots for pkvm to fill when it releases pages.
 */
#define PKVM_MAX_NR_DONATED_PAGES	256

/*
 * Generic hypercall parameter for clearing legacy
 * and scalable mode context entries and pasid table
 * entries.
 */
struct pkvm_clear_translation_param {
	u64 phys;
	u16 bdf;
	u32 pasid;
	u8 ats_enabled;
	u8 ats_qdep;
	u8 dtlb_extra_inval;
};

struct pkvm_lm_context_param {
	u64 phys;
	u16 bdf;
	u16 did;
	u16 ats_supported;
	u16 domain_agaw;
	u64 domain_pgd_gpa;
	u64 context_gpa;
};

struct pkvm_sm_context_param {
	u64 phys;
	u16 bdf;
	u8 ats_supported;
	u8 pasid_supported;
	u32 max_pasid;
	u64 pasid_dir_gpa;
	u64 context_gpa;
};

struct pkvm_pasid_table_param {
	u64 phys;
	u16 bdf;
	u16 did;
	u32 pasid;
	u8 force_snooping;
	u8 dirty_tracking;
	u32 max_pasid;
	u64 pasid_dir_gpa;
	u64 pasid_table_gpa;
	u16 domain_gaw;
	u16 domain_agaw;
	u64 domain_pgd_gpa;
};

struct pkvm_domain_param {
	u64 phys;
	u16 bdf;
	u16 gaw;
	u8 agaw;
	u8 iommu_superpage;
	u8 iommu_coherency;
	u8 use_first_level;
	u64 max_addr;
	u64 pgd_gpa;
};

struct pkvm_cache_tag_param {
	u64 phys;
	u64 pgd_gpa;
	int type;
	u8 bus;
	u8 devfn;
	u16 pfsid;
	u8 ats_qdep;
	u8 dtlb_extra_inval;
	u16 domain_id;
	u32 pasid;
};

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
		return (u64)pkvm_hypercall(iommu_mmio_access, true,
					   sizeof(u64), reg_phys + offset);
	else
		return readq(reg + offset);
}

static inline u32 pkvm_readl(void __iomem *reg, unsigned long reg_phys,
			     unsigned long offset)
{
	if (pkvm_enabled())
		return (u32)pkvm_hypercall(iommu_mmio_access, true,
					   sizeof(u32), reg_phys + offset);
	else
		return readl(reg + offset);
}

static inline void pkvm_writeq(void __iomem *reg, unsigned long reg_phys,
			       unsigned long offset, u64 val)
{
	if (pkvm_enabled())
		pkvm_hypercall(iommu_mmio_access, false, sizeof(u64),
			       reg_phys + offset, val);
	else
		writeq(val, reg + offset);
}

static inline void pkvm_writel(void __iomem *reg, unsigned long reg_phys,
			       unsigned long offset, u32 val)
{
	if (pkvm_enabled())
		pkvm_hypercall(iommu_mmio_access, false, sizeof(u32),
			       reg_phys + offset, (u64)val);
	else
		writel(val, reg + offset);
}

DECLARE_PER_CPU(union pkvm_iommu_page_donation, iommu_page_donation);

#else /* __PKVM_HYP__ */

/* we are in pkvm hypervisor, pkvm is enabled by definition */
#define enable_pkvm true

DECLARE_PER_CPU(union pkvm_iommu_page_donation, *iommu_page_donation);

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

#ifdef CONFIG_PKVM_INTEL_PVIOMMU
		/*
		 * Disable PRS(Page Request Support) for simplicity.
		 * Users of PRS are currently iommufd and user mode iommu management.
		 * Disabling for simplicity as pkvm doesn't yet support
		 * device assignment and PTL and ADL doesn't have PRS enabled.
		 * Will revisit this later with device assignment feature.
		 */
		*ecap &= ~(1UL << 29);
#endif

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
