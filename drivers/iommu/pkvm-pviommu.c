// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Google LLC
 * Author: Mostafa Saleh <smostafa@google.com>
 */
#include <linux/of_platform.h>
#include <linux/arm-smccc.h>
#include <linux/iommu.h>
#include <linux/maple_tree.h>
#include <linux/pci.h>
#include <linux/xarray.h>

#define ASSERT(cond)							\
	do {								\
		if (!(cond)) {						\
			pr_err("line %d: assertion failed: %s\n",	\
			       __LINE__, #cond);			\
			return -1;					\
		}							\
	} while (0)

#define FEAUTRE_PGSIZE_BITMAP		0x1
#define DRIVER_VERSION			0x1000ULL

struct pviommu {
	struct iommu_device		iommu;
	u32				id;
	u32				pgsize_bitmap;
};

struct pviommu_domain {
	struct iommu_domain		domain;
	unsigned long			id; /* pKVM domain ID. */
	struct maple_tree		mappings; /* IOVA -> IPA */
};

struct pviommu_master {
	struct device			*dev;
	struct pviommu			*iommu;
	u32				ssid_bits;
	struct pviommu_domain		*domain;
};

static DEFINE_XARRAY(pviommu_groups);

static u64 __linux_prot_smccc(int iommu_prot)
{
	int prot = 0;

	if (iommu_prot & IOMMU_READ)
		prot |= ARM_SMCCC_KVM_PVIOMMU_READ;
	if (iommu_prot & IOMMU_WRITE)
		prot |= ARM_SMCCC_KVM_PVIOMMU_WRITE;
	if (iommu_prot & IOMMU_CACHE)
		prot |= ARM_SMCCC_KVM_PVIOMMU_CACHE;
	if (iommu_prot & IOMMU_NOEXEC)
		prot |= ARM_SMCCC_KVM_PVIOMMU_NOEXEC;
	if (iommu_prot & IOMMU_MMIO)
		prot |= ARM_SMCCC_KVM_PVIOMMU_MMIO;
	if (iommu_prot & IOMMU_PRIV)
		prot |= ARM_SMCCC_KVM_PVIOMMU_PRIV;

	return prot;
}

/* Ranges are inclusive for all functions. */
static void pviommu_domain_insert_map(struct pviommu_domain *pv_domain,
				      u64 start, u64 end, u64 val)
{
	if (end < start)
		return;

	mtree_store_range(&pv_domain->mappings, start, end, xa_mk_value(val), GFP_KERNEL);
}

static void pviommu_domain_remove_map(struct pviommu_domain *pv_domain,
				      u64 start, u64 end)
{
	/* Range can cover multiple entries. */
	while (start < end) {
		MA_STATE(mas, &pv_domain->mappings, start, end);
		u64 entry = xa_to_value(mas_find(&mas, start));
		u64 old_start, old_end;

		old_start = mas.index;
		old_end = mas.last;
		mas_erase(&mas);
		/* Insert the rest if no removed*/
		if (start > old_start)
			mtree_store_range(&pv_domain->mappings, old_start, start - 1,
					  xa_mk_value(entry), GFP_KERNEL);

		if (old_end > end)
			mtree_store_range(&pv_domain->mappings, end + 1, old_end,
					  xa_mk_value(entry + end - old_start + 1), GFP_KERNEL);

		start = old_end + 1;
	}
}

static u64 pviommu_domain_find(struct pviommu_domain *pv_domain, u64 key)
{
	MA_STATE(mas, &pv_domain->mappings, key, key);
	void *entry = mas_find(&mas, key);

	/* No entry. */
	if (!xa_is_value(entry))
		return 0;

	return (key - mas.index) + (u64)xa_to_value(entry);
}

static int pviommu_map_pages(struct iommu_domain *domain, unsigned long iova,
			     phys_addr_t paddr, size_t pgsize, size_t pgcount,
			     int prot, gfp_t gfp, size_t *mapped)
{
	int ret;
	struct pviommu_domain *pv_domain = container_of(domain, struct pviommu_domain, domain);
	struct arm_smccc_res res;
	size_t requested_size = pgsize * pgcount, cur_mapped;

	*mapped = 0;
	while (*mapped < requested_size) {
		arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_MAP_FUNC_ID,
				  pv_domain->id, iova, paddr, pgsize, pgcount,
				  __linux_prot_smccc(prot), &res);
		cur_mapped = res.a1;
		ret = res.a0;
		*mapped += cur_mapped;
		iova += cur_mapped;
		paddr += cur_mapped;
		pgcount -= cur_mapped / pgsize;
		if (ret)
			break;
	}

	if (*mapped)
		pviommu_domain_insert_map(pv_domain, iova - *mapped, iova - 1, paddr - *mapped);

	return ret;
}

static size_t pviommu_unmap_pages(struct iommu_domain *domain, unsigned long iova,
				  size_t pgsize, size_t pgcount,
				  struct iommu_iotlb_gather *gather)
{
	int ret;
	struct pviommu_domain *pv_domain = container_of(domain, struct pviommu_domain, domain);
	struct arm_smccc_res res;
	size_t total_unmapped = 0, unmapped, requested_size = pgsize * pgcount;

	while (total_unmapped < requested_size) {
		arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_UNMAP_FUNC_ID,
				  pv_domain->id, iova, pgsize, pgcount, &res);
		ret = res.a0;
		unmapped = res.a1;
		total_unmapped += unmapped;
		iova += unmapped;
		pgcount -= unmapped / pgsize;
		if (ret)
			break;
	}

	if (total_unmapped)
		pviommu_domain_remove_map(pv_domain, iova - total_unmapped, iova - 1);

	return total_unmapped;
}

static phys_addr_t pviommu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	struct pviommu_domain *pv_domain = container_of(domain, struct pviommu_domain, domain);

	return pviommu_domain_find(pv_domain, iova);
}

static void pviommu_domain_free(struct iommu_domain *domain)
{
	struct pviommu_domain *pv_domain = container_of(domain, struct pviommu_domain, domain);
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_FREE_DOMAIN_FUNC_ID,
			  pv_domain->id, &res);
	if (res.a0 != SMCCC_RET_SUCCESS)
		pr_err("Failed to free domain %ld\n", res.a0);

	mtree_destroy(&pv_domain->mappings);
	kfree(pv_domain);
}

static int smccc_to_linux_ret(u64 smccc_ret)
{
	switch (smccc_ret) {
	case SMCCC_RET_SUCCESS:
		return 0;
	case SMCCC_RET_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case SMCCC_RET_NOT_REQUIRED:
		return -ENOENT;
	case SMCCC_RET_INVALID_PARAMETER:
		return -EINVAL;
	};

	return -ENODEV;
}

static int pviommu_set_dev_pasid(struct iommu_domain *domain,
				 struct device *dev, ioasid_t pasid)
{
	int ret = 0, i;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct pviommu *pv;
	struct pviommu_domain *pv_domain = container_of(domain, struct pviommu_domain, domain);
	struct pviommu_master *master;
	struct arm_smccc_res res;
	u32 sid;

	if (!fwspec)
		return -ENOENT;

	master = dev_iommu_priv_get(dev);
	pv = master->iommu;
	master->domain = pv_domain;

	for (i = 0; i < fwspec->num_ids; i++) {
		sid = fwspec->ids[i];
		arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_ATTACH_DEV_FUNC_ID,
				  pv->id, sid, pasid,
				  pv_domain->id, master->ssid_bits, &res);
		if (res.a0) {
			ret = smccc_to_linux_ret(res.a0);
			break;
		}
	}

	if (ret) {
		while (i--) {
			arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_DETACH_DEV_FUNC_ID,
					  pv->id, sid, 0 /* PASID */,
					  pv_domain->id, &res);
		}
	}

	return ret;
}

static int pviommu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	return pviommu_set_dev_pasid(domain, dev, 0);
}

static void pviommu_remove_dev_pasid(struct device *dev, ioasid_t pasid)
{
	int i;
	struct pviommu_master *master = dev_iommu_priv_get(dev);
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct pviommu *pv = master->iommu;
	struct pviommu_domain *pv_domain = master->domain;
	struct arm_smccc_res res;
	u32 sid;

	if (!fwspec)
		return;

	for (i = 0; i < fwspec->num_ids; i++) {
		sid = fwspec->ids[i];
		arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_DETACH_DEV_FUNC_ID,
				  pv->id, sid, pasid, pv_domain->id, &res);
		if (res.a0 != SMCCC_RET_SUCCESS)
			dev_err(dev, "Failed to detach_dev sid %d, err %ld\n", sid, res.a0);
	}
}

static void pviommu_detach_dev(struct device *dev)
{
	pviommu_remove_dev_pasid(dev, 0);
}

static struct iommu_domain *pviommu_domain_alloc(unsigned int type)
{
	struct pviommu_domain *pv_domain;
	struct arm_smccc_res res;

	if (type != IOMMU_DOMAIN_UNMANAGED &&
	    type != IOMMU_DOMAIN_DMA)
		return NULL;

	pv_domain = kzalloc(sizeof(*pv_domain), GFP_KERNEL);
	if (!pv_domain)
		return NULL;

	mt_init(&pv_domain->mappings);

	arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_ALLOC_DOMAIN_FUNC_ID, &res);

	if (res.a0 != SMCCC_RET_SUCCESS) {
		kfree(pv_domain);
		return NULL;
	}

	pv_domain->id = res.a1;

	return &pv_domain->domain;
}

static struct platform_driver pkvm_pviommu_driver;

static struct pviommu *pviommu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev = driver_find_device_by_fwnode(&pkvm_pviommu_driver.driver, fwnode);

	put_device(dev);
	return dev ? dev_get_drvdata(dev) : NULL;
}

static struct iommu_ops pviommu_ops;

static struct iommu_device *pviommu_probe_device(struct device *dev)
{
	struct pviommu_master *master;
	struct pviommu *pv = NULL;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!fwspec || fwspec->ops != &pviommu_ops)
		return ERR_PTR(-ENODEV);

	pv = pviommu_get_by_fwnode(fwspec->iommu_fwnode);
	if (!pv)
		return ERR_PTR(-ENODEV);

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return ERR_PTR(-ENOMEM);

	master->dev = dev;
	master->iommu = pv;
	device_property_read_u32(dev, "pasid-num-bits", &master->ssid_bits);
	dev_iommu_priv_set(dev, master);

	return &pv->iommu;
}

static void pviommu_release_device(struct device *dev)
{
	pviommu_detach_dev(dev);
}

static int pviommu_of_xlate(struct device *dev, struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, args->args_count);
}

static struct iommu_group *pviommu_group_alloc_get(struct device *dev, int group_id)
{
	struct iommu_group *group;

	group = xa_load(&pviommu_groups, (unsigned long)group_id);
	if (group)
		return group;
	group = iommu_group_alloc();
	if (!IS_ERR(group))
		return NULL;

	if (xa_insert(&pviommu_groups, (unsigned long)group_id, group, GFP_KERNEL))
		dev_err(dev,
			"Failed to track group %d this will lead to multiple groups instead of one\n",
			group_id);

	return group;
}

static struct iommu_group *pviommu_device_group(struct device *dev)
{
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!fwspec)
		return NULL;

	if (dev_is_pci(dev))
		return pci_device_group(dev);
	else {
		if (fwspec->num_ids == 1)
			return generic_device_group(dev);
		else
			return pviommu_group_alloc_get(dev, fwspec->ids[1]);
	}
}

static struct iommu_ops pviommu_ops = {
	.device_group		= pviommu_device_group,
	.of_xlate		= pviommu_of_xlate,
	.probe_device		= pviommu_probe_device,
	.release_device		= pviommu_release_device,
	.domain_alloc		= pviommu_domain_alloc,
	.remove_dev_pasid	= pviommu_remove_dev_pasid,
	.owner			= THIS_MODULE,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev	= pviommu_attach_dev,
		.map_pages	= pviommu_map_pages,
		.unmap_pages	= pviommu_unmap_pages,
		.iova_to_phys	= pviommu_iova_to_phys,
		.set_dev_pasid	= pviommu_set_dev_pasid,
		.free		= pviommu_domain_free,
	}
};

static int pviommu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pviommu *pv = devm_kmalloc(dev, sizeof(*pv), GFP_KERNEL);
	struct device_node *np = pdev->dev.of_node;
	int ret;
	struct arm_smccc_res res;
	u64 version;

	ret = of_property_read_u32_index(np, "id", 0, &pv->id);
	if (ret) {
		dev_err(dev, "Failed to read id from device tree node %d\n", ret);
		return ret;
	}

	arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_VERSION_FUNC_ID, &res);
	if (res.a0 != SMCCC_RET_SUCCESS)
		return -ENODEV;
	version = res.a1;
	if (version != DRIVER_VERSION)
		pr_warn("pviommu driver expects version %llx but found %llx\n",
			DRIVER_VERSION, version);

	arm_smccc_1_1_hvc(ARM_SMCCC_VENDOR_HYP_KVM_IOMMU_GET_FEATURE_FUNC_ID,
			  pv->id, FEAUTRE_PGSIZE_BITMAP, &res);
	if (res.a0 != SMCCC_RET_SUCCESS)
		return -ENODEV;

	pv->pgsize_bitmap = pviommu_ops.pgsize_bitmap = res.a1;

	ret = iommu_device_sysfs_add(&pv->iommu, dev, NULL,
				     "pviommu.%pa", &pv->id);

	ret = iommu_device_register(&pv->iommu, &pviommu_ops, dev);
	if (ret) {
		dev_err(dev, "Couldn't register %d\n", ret);
		iommu_device_sysfs_remove(&pv->iommu);
	}

	platform_set_drvdata(pdev, pv);

	return ret;
}

static const struct of_device_id pviommu_of_match[] = {
	{ .compatible = "pkvm,pviommu", },
	{ },
};

static struct platform_driver pkvm_pviommu_driver = {
	.probe = pviommu_probe,
	.driver = {
		.name = "pkvm-pviommu",
		.of_match_table = pviommu_of_match,
	},
};

#if IS_ENABLED(CONFIG_PKVM_PVIOMMU_SELFTEST) && !defined(MODULE)
/* Mainly test iova_to_phys and not hypervisor interface. */
int __init __pviommu_selftest(void)
{
	struct pviommu_domain domain;

	pr_info("pviommu selftest starting\n");

	mt_init(&domain.mappings);

	pviommu_domain_insert_map(&domain, 0x10000, 0xFEFFF, 0xE0000);
	pviommu_domain_insert_map(&domain, 0xFFF0000, 0x1EDBFFFF, 0xDEAD0000);
	ASSERT(pviommu_domain_find(&domain, 0x10000) == 0xE0000);
	ASSERT(pviommu_domain_find(&domain, 0x10F00) == 0xE0F00);
	ASSERT(pviommu_domain_find(&domain, 0x1EDBFFFF) == 0xED89FFFF);
	ASSERT(pviommu_domain_find(&domain, 0x10000000) == 0xDEAE0000);
	ASSERT(pviommu_domain_find(&domain, 0x1FF000) == 0);
	pviommu_domain_remove_map(&domain, 0x12000, 0x19FFF);
	ASSERT(pviommu_domain_find(&domain, 0x11000) == 0xE1000);
	ASSERT(pviommu_domain_find(&domain, 0x1B000) == 0xEB000);
	ASSERT(pviommu_domain_find(&domain, 0x14000) == 0);

	pviommu_domain_insert_map(&domain, 0xC00000, 0xCFFFFF, 0xABCD000);
	pviommu_domain_insert_map(&domain, 0xD00000, 0xDFFFFF, 0x1000);
	pviommu_domain_insert_map(&domain, 0xE00000, 0xEFFFFF, 0xC0FE00000);
	ASSERT(pviommu_domain_find(&domain, 0xD00000) == 0x1000);
	pviommu_domain_remove_map(&domain, 0xC50000, 0xE5FFFF);
	ASSERT(pviommu_domain_find(&domain, 0xC50000) == 0);
	ASSERT(pviommu_domain_find(&domain, 0xD10000) == 0);
	ASSERT(pviommu_domain_find(&domain, 0xE60000) == 0xC0FE60000);
	ASSERT(pviommu_domain_find(&domain, 0xC10000) == 0xABDD000);

	mtree_destroy(&domain.mappings);
	return 0;
}

subsys_initcall(__pviommu_selftest);
#endif

module_platform_driver(pkvm_pviommu_driver);

MODULE_DESCRIPTION("IOMMU API for pKVM paravirtualized IOMMU");
MODULE_AUTHOR("Mostafa Saleh <smostafa@google.com>");
MODULE_LICENSE("GPL v2");
