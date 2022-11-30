// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 */

#include <asm/io-mpt-s2mpu.h>

static u32 this_version;

static void __set_l1entry_attr_with_prot(void *dev_va, unsigned int gb,
					 unsigned int vid, enum mpt_prot prot)
{
	writel_relaxed(L1ENTRY_ATTR_1G(prot),
		       dev_va + REG_NS_L1ENTRY_ATTR(vid, gb));
}

static void __set_l1entry_l2table_addr(void *dev_va, unsigned int gb,
				       unsigned int vid, phys_addr_t addr)
{
	/* Order against writes to the SMPT. */
	writel(L1ENTRY_L2TABLE_ADDR(addr),
	       dev_va + REG_NS_L1ENTRY_L2TABLE_ADDR(vid, gb));
}

static void init_with_prot(void *dev_va, enum mpt_prot prot)
{
	unsigned int gb, vid;

	for_each_gb_and_vid(gb, vid)
		__set_l1entry_attr_with_prot(dev_va, gb, vid, prot);
}

/*
 * Create instance for each s2mpu version using templates
 * This will few extra KBs, but should improve performance.
 */
#define ARG_PROT_BITS		MPT_PROT_BITS
#define ARG_ACCESS_SHIFT	MPT_ACCESS_SHIFT
#define ARG_GRAN_MASK		L1ENTRY_ATTR_GRAN(SMPT_GRAN_ATTR, L1ENTRY_ATTR_GRAN_MASK)
#define ARG_LUT_PROT		mpt_prot_doubleword
#define T_FUNC(fn)		s2mpu_v1 ## _ ##fn
#include <asm/s2mpu_mpt_template.h>
#undef ARG_PROT_BITS
#undef ARG_ACCESS_SHIFT
#undef ARG_GRAN_MASK
#undef ARG_LUT_PROT
#undef T_FUNC

#define ARG_PROT_BITS		V9_MPT_PROT_BITS
#define ARG_ACCESS_SHIFT	V9_MPT_ACCESS_SHIFT
#define ARG_GRAN_MASK		L1ENTRY_ATTR_GRAN(SMPT_GRAN_ATTR, V9_L1ENTRY_ATTR_GRAN_MASK)
#define ARG_LUT_PROT		v9_mpt_prot_doubleword
#define T_FUNC(fn)		s2mpu_v9 ## _ ##fn
#include <asm/s2mpu_mpt_template.h>
#undef ARG_PROT_BITS
#undef ARG_ACCESS_SHIFT
#undef ARG_GRAN_MASK
#undef ARG_LUT_PROT
#undef T_FUNC

static const struct s2mpu_mpt_ops v1v2_ops = {
	.smpt_size = s2mpu_v1_smpt_size,
	.init_with_prot = init_with_prot,
	.init_with_mpt = s2mpu_v1_init_with_mpt,
	.apply_range = s2mpu_v1_apply_range,
	.prepare_range = s2mpu_v1_prepare_range,
	.pte_from_addr_smpt = s2mpu_v1_pte_from_addr_smpt,
};
static const struct s2mpu_mpt_ops v9_ops = {
	.smpt_size = s2mpu_v9_smpt_size,
	.init_with_prot = init_with_prot,
	.init_with_mpt = s2mpu_v9_init_with_mpt,
	.apply_range = s2mpu_v9_apply_range,
	.prepare_range = s2mpu_v9_prepare_range,
	.pte_from_addr_smpt = s2mpu_v9_pte_from_addr_smpt,
};

const struct s2mpu_mpt_ops *s2mpu_get_mpt_ops(struct s2mpu_mpt_cfg cfg)
{
/* allow EL1 to register multiple versions at same runtime as this is used in self tests */
#ifdef __KVM_NVHE_HYPERVISOR__
	/* If called before with different version return NULL. */
	if (WARN_ON(this_version && (this_version != cfg.version)))
		return NULL;
#endif
	/* 2MB granularity not supported in V9 */
	if ((cfg.version == S2MPU_VERSION_9) && (SMPT_GRAN_ATTR != L1ENTRY_ATTR_GRAN_2M)) {
		this_version = cfg.version;
		return &v9_ops;
	} else if ((cfg.version == S2MPU_VERSION_2) || (cfg.version == S2MPU_VERSION_1)) {
		this_version = cfg.version;
		return &v1v2_ops;
	}
	return NULL;
}
