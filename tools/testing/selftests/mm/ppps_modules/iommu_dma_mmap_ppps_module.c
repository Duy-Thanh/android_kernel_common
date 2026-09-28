// SPDX-License-Identifier: GPL-2.0
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "../../ppps/ppps_misc_module.h"

#define TEST_VENDOR 0x1234
#define TEST_DEVICE 0x11e8
#define TEST_SIZE (4 * 4096UL)

static struct pci_dev *test_pdev;
static struct sg_table *test_sgt;
static void *test_vaddr;

static int test_mmap(struct file *file, struct vm_area_struct *vma)
{
	return dma_mmap_noncontiguous(&test_pdev->dev, vma, TEST_SIZE,
				      test_sgt);
}

static const struct file_operations test_fops = {
	.owner = THIS_MODULE,
	.mmap = test_mmap,
};

static void test_free_buffer(void)
{
	if (test_vaddr) {
		dma_vunmap_noncontiguous(&test_pdev->dev, test_vaddr);
		test_vaddr = NULL;
	}
	if (test_sgt) {
		dma_free_noncontiguous(&test_pdev->dev, TEST_SIZE, test_sgt,
				       DMA_BIDIRECTIONAL);
		test_sgt = NULL;
	}
}

static int test_setup(void)
{
	size_t offset;
	int error;
	test_pdev = pci_get_device(TEST_VENDOR, TEST_DEVICE, NULL);
	if (!test_pdev)
		return -ENODEV;
	if (!device_iommu_mapped(&test_pdev->dev)) {
		error = -ENODEV;
		goto put_device;
	}
	error = dma_set_mask_and_coherent(&test_pdev->dev, DMA_BIT_MASK(64));
	if (error)
		goto put_device;

	test_sgt = dma_alloc_noncontiguous(&test_pdev->dev, TEST_SIZE,
					   DMA_BIDIRECTIONAL, GFP_KERNEL, 0);
	if (!test_sgt) {
		error = -ENOMEM;
		goto put_device;
	}
	test_vaddr = dma_vmap_noncontiguous(&test_pdev->dev, TEST_SIZE,
					    test_sgt);
	if (!test_vaddr) {
		error = -ENOMEM;
		goto free_buffer;
	}
	for (offset = 0; offset < TEST_SIZE; offset += 4096)
		((unsigned char *)test_vaddr)[offset] = 0x51 + offset / 4096;
	return 0;

free_buffer:
	test_free_buffer();
put_device:
	pci_dev_put(test_pdev);
	test_pdev = NULL;
	return error;
}

static void test_teardown(void)
{
	test_free_buffer();
	pci_dev_put(test_pdev);
}

PPPS_MISC_MODULE("iommu_dma_mmap_ppps", &test_fops, 0, test_setup, test_teardown,
		 "Test noncontiguous IOMMU DMA mmap with PPPS");
