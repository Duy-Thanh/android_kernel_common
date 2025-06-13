// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_address.h>

static int __init dma_pool_glue_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reserved_mem *rmem;
	int ret;

	dev_info(dev, "calling dma_pool_glue_probe()\n");

	rmem = of_reserved_mem_lookup(dev->of_node);
	if (!rmem) {
		dev_err(dev, "dma_pool_glue: failed to lookup reserved memory\n");
		return -EINVAL;
	}

	if (!rmem->size || (rmem->size > ULONG_MAX)) {
		dev_err(dev, "dma_pool_glue: invalid memory region size\n");
		return -EINVAL;
	}

	if (!PAGE_ALIGNED(rmem->base) || !PAGE_ALIGNED(rmem->size)) {
		dev_err(dev, "dma_pool_glue: memory region must be page-aligned\n");
		return -EINVAL;
	}

	ret = dma_declare_coherent_memory(dev, rmem->base, rmem->base, rmem->size);
	if (ret) {
		dev_err(dev, "dma_pool_glue: Failed to declare coherent memory pool: %d\n", ret);
		return ret;
	}

	dev_info(dev, "dma_pool_glue: Successfully attached custom DMA pool from DT\n");

	return 0;
}

static int dma_pool_glue_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	dma_release_coherent_memory(dev);
	dev_info(dev, "dma_pool_glue: Released custom DMA pool\n");

	return 0;
}

static const struct of_device_id dma_pool_glue_of_match[] = {
	{ .compatible = "google,uvc-urb" },
	{},
};

static struct platform_driver dma_pool_glue_driver = {
	.remove = dma_pool_glue_remove,
	.driver = {
		.name = "dma-pool-glue",
		.of_match_table = dma_pool_glue_of_match,
	},
};

static int __init dma_pool_glue_init(void)
{
	int ret;

	ret = platform_driver_probe(&dma_pool_glue_driver, dma_pool_glue_probe);

	return (ret == -ENODEV) ? 0 : ret;
}

static void __exit dma_pool_glue_exit(void)
{
	platform_driver_unregister(&dma_pool_glue_driver);
}

module_init(dma_pool_glue_init);
module_exit(dma_pool_glue_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DT-aware glue driver for allocating from a host DMA pool.");
