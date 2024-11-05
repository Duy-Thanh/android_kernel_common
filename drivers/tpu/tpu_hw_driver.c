// SPDX-License-Identifier: GPL-2.0

/*
 * Platform device driver to test CMA allocations
 *
 * The expected structure of the device tree for the dummy TPU is:
 *
 * / {
 *
 *        ......
 *
 *        tpu_cma_reserve: tpu_cma_reserve {
 *            compatible = "shared-dma-pool";
 *            reusable;
 *            size = <0x0  0x400000>;
 *            alignment = <0x0 0x00010000>;
 *            alloc-ranges = <0x0 0x9 0x80000000 0x80000000>,
 *                           <0x0 0x9 0x00000000 0x80000000>;
 *        };
 *    };
 *
 *    tpu_hw_node {
 *        compatible = "tpu_hw,dummy";
 *        memory-region = <&tpu_cma_reserve>;
 *        state = "active";
 *    };
 */

#define pr_fmt(fmt)  "%s: %s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/printk.h>

static const struct platform_device_id tpu_hw_id_table[] = {
	{ "tpu_plat_dev_id", 23 },
	{}, // Needs to finish with a NULL entry.
};
MODULE_DEVICE_TABLE(platform, tpu_hw_id_table);

// Identifies the node in the device tree.
static const struct of_device_id tpu_dt_ids[] = {
	{ .compatible = "tpu_hw_dummy" },
	{}, // Needs to finish with a NULL entry.
};
MODULE_DEVICE_TABLE(of, tpu_dt_ids);

static void rmem_remove_callback(void *p)
{
	of_reserved_mem_device_release((struct device *)p);
}

static int tpu_hw_probe(struct platform_device *pdev)
{
	int ret;
	const char *state;
	const struct of_device_id *of_id;

	pr_info("Probing device");

	of_id = of_match_device(tpu_dt_ids, &pdev->dev);
	if (!of_id) {
		pr_info("The node was not found in DTB");
		return -ENODEV;
	}

	ret = of_reserved_mem_device_init(&pdev->dev);
	if (ret || !pdev->dev.cma_area) {
		dev_err(&pdev->dev, "The CMA reserved area is not assigned (ret %d)\n", ret);
		return -EINVAL;
	}

	ret = devm_add_action(&pdev->dev, rmem_remove_callback, &pdev->dev);
	if (ret) {
		of_reserved_mem_device_release(&pdev->dev);
		return ret;
	}

	ret = of_property_read_string(pdev->dev.of_node, "state", &state);
	if (ret < 0) {
		pr_info("Failed to read 'state'");
		return ret;
	}

	pr_info("state = %s", state);

	return 0;
}

static int tpu_hw_remove(struct platform_device *pdev)
{
	pr_info("Remove TPU");

	return 0;
}

static void tpu_hw_shutdown(struct platform_device *pdev)
{
	pr_info("Shutdown TPU");
}

static int tpu_hw_suspend(struct platform_device *pdev, pm_message_t state)
{
	pr_info("Suspend TPU");

	return 0;
}

static int tpu_hw_resume(struct platform_device *pdev)
{
	pr_info("Resume TPU");

	return 0;
}

static struct platform_driver tpu_hw_driver = {
	.driver = {
		.name = "tpu_hw_dummy",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(tpu_dt_ids),
	},
	.probe = tpu_hw_probe,
	.remove = tpu_hw_remove,
	.suspend = tpu_hw_suspend,
	.shutdown = tpu_hw_shutdown,
	.resume = tpu_hw_resume,
	.id_table = tpu_hw_id_table,
};

module_platform_driver(tpu_hw_driver);

MODULE_DESCRIPTION("Dummy TPU HW Platform driver");
MODULE_LICENSE("GPL v2");


