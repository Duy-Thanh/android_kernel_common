// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "../../ppps/ppps_misc_module.h"

#define USER_PAGE_SIZE	4096UL
#define TEST_BUFFER_SIZE	(4 * USER_PAGE_SIZE)

static struct platform_device *test_pdev;
static dma_addr_t test_dma_addr;
static void *test_cpu_addr;

static int dma_mmap_attrs_ppps_mmap(struct file *file,
				    struct vm_area_struct *vma)
{
	return dma_mmap_attrs(&test_pdev->dev, vma, test_cpu_addr,
			      test_dma_addr, TEST_BUFFER_SIZE, 0);
}

static const struct file_operations dma_mmap_attrs_ppps_fops = {
	.owner = THIS_MODULE,
	.mmap = dma_mmap_attrs_ppps_mmap,
};

static int dma_mmap_attrs_ppps_setup(void)
{
	unsigned int offset;
	int error;

	test_pdev = platform_device_register_simple("dma-mmap-attrs-ppps",
						    PLATFORM_DEVID_NONE,
						    NULL, 0);
	if (IS_ERR(test_pdev))
		return PTR_ERR(test_pdev);
	test_pdev->dev.dma_mask = &test_pdev->dev.coherent_dma_mask;
	test_pdev->dev.dma_coherent = true;
	error = dma_set_mask_and_coherent(&test_pdev->dev, DMA_BIT_MASK(64));
	if (error)
		goto unregister_device;

	test_cpu_addr = dma_alloc_attrs(&test_pdev->dev, TEST_BUFFER_SIZE,
					&test_dma_addr, GFP_KERNEL, 0);
	if (!test_cpu_addr) {
		error = -ENOMEM;
		goto unregister_device;
	}
	for (offset = 0; offset < TEST_BUFFER_SIZE; offset += USER_PAGE_SIZE)
		memset(test_cpu_addr + offset, 0x61 + offset / USER_PAGE_SIZE,
		       USER_PAGE_SIZE);
	return 0;

unregister_device:
	platform_device_unregister(test_pdev);
	return error;
}

static void dma_mmap_attrs_ppps_teardown(void)
{
	dma_free_attrs(&test_pdev->dev, TEST_BUFFER_SIZE, test_cpu_addr,
		       test_dma_addr, 0);
	platform_device_unregister(test_pdev);
}

PPPS_MISC_MODULE("dma_mmap_attrs_ppps", &dma_mmap_attrs_ppps_fops, 0,
		 dma_mmap_attrs_ppps_setup, dma_mmap_attrs_ppps_teardown,
		 "PPPS dma_mmap_attrs regression test helper");
