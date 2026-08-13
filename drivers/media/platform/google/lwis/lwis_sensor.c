// SPDX-License-Identifier: GPL-2.0-only
/*
 * Google LWIS (Lightweight Image Subsystem) Sensor Driver
 * Support for Sony IMX08B (ICHI) and generic LWIS image sensors.
 *
 * Copyright (C) 2026 Google LLC.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>

#define DRIVER_NAME "lwis_sensor"
#define DEV_NAME_ICHI "lwis-sensor-ichi"
#define DEV_NAME_GENERIC "lwis-sensor"

#define SONY_IMX08B_MODEL_ID 0x008B
#define SONY_REG_MODEL_ID_MSB 0x0000
#define SONY_REG_MODEL_ID_LSB 0x0001

struct lwis_power_delays {
	u32 t_vana1_vdig_ms;
	u32 t_vdig_vana2_ms;
	u32 t_vana2_vif_ms;
	u32 t4_pwr_to_xclr_ms;
	u32 t5_xclr_to_i2c_ms;
	u32 t6_xclr_to_stream_ms;
};

struct lwis_sensor_dev {
	struct device *dev;
	struct i2c_client *i2c_client;
	struct cdev cdev;
	dev_t dev_num;
	struct class *class;
	struct device *char_dev;

	/* Clock */
	struct clk *mclk;
	u32 mclk_freq_hz;

	/* Power Regulators */
	struct regulator *vana1;
	struct regulator *vana2;
	struct regulator *vdig;
	struct regulator *vif;
	struct regulator *vaf;

	/* Power Sequence Delays & Slew Rate */
	struct lwis_power_delays delays;
	u32 max_slew_rate_mv_per_us;

	/* GPIOs */
	struct gpio_desc *reset_gpio;
	struct gpio_desc *xvs_gpio;

	/* I2C Info */
	u32 i2c_addr;
	u32 i2c_clk_freq;

	/* State */
	bool is_powered_on;
	bool is_ichi;
	char node_name[64];
};

static dev_t g_lwis_dev_num;
static struct class *g_lwis_class;
static int g_lwis_major = 242;

/* File Operations for /dev/lwis-sensor-ichi */
static int lwis_sensor_open(struct inode *inode, struct file *file)
{
	struct lwis_sensor_dev *sensor_dev =
		container_of(inode->i_cdev, struct lwis_sensor_dev, cdev);
	file->private_data = sensor_dev;
	return 0;
}

static int lwis_sensor_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t lwis_sensor_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t lwis_sensor_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	return count;
}

static long lwis_sensor_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	return 0;
}

static const struct file_operations lwis_sensor_fops = {
	.owner = THIS_MODULE,
	.open = lwis_sensor_open,
	.release = lwis_sensor_release,
	.read = lwis_sensor_read,
	.write = lwis_sensor_write,
	.unlocked_ioctl = lwis_sensor_ioctl,
};

/* Power management helpers */
static int lwis_sensor_power_on(struct lwis_sensor_dev *sdev)
{
	int ret;

	if (sdev->is_powered_on)
		return 0;

	/* 1. VANA1 (2.8V) */
	if (sdev->vana1) {
		ret = regulator_enable(sdev->vana1);
		if (ret) {
			dev_err(sdev->dev, "Failed to enable vana1: %d\n", ret);
			return ret;
		}
	}
	if (sdev->delays.t_vana1_vdig_ms)
		msleep(sdev->delays.t_vana1_vdig_ms);

	/* 2. VDIG (1.1V) */
	if (sdev->vdig) {
		ret = regulator_enable(sdev->vdig);
		if (ret) {
			dev_err(sdev->dev, "Failed to enable vdig: %d\n", ret);
			goto err_vdig;
		}
	}
	if (sdev->delays.t_vdig_vana2_ms)
		msleep(sdev->delays.t_vdig_vana2_ms);

	/* 3. VANA2 (1.8V) */
	if (sdev->vana2) {
		ret = regulator_enable(sdev->vana2);
		if (ret) {
			dev_err(sdev->dev, "Failed to enable vana2: %d\n", ret);
			goto err_vana2;
		}
	}
	if (sdev->delays.t_vana2_vif_ms)
		msleep(sdev->delays.t_vana2_vif_ms);

	/* 4. VIF (1.8V) */
	if (sdev->vif) {
		ret = regulator_enable(sdev->vif);
		if (ret) {
			dev_err(sdev->dev, "Failed to enable vif: %d\n", ret);
			goto err_vif;
		}
	}

	/* 5. VAF (2.8V) */
	if (sdev->vaf) {
		ret = regulator_enable(sdev->vaf);
		if (ret) {
			dev_err(sdev->dev, "Failed to enable vaf: %d\n", ret);
			goto err_vaf;
		}
	}

	/* Master Clock enable */
	if (sdev->mclk) {
		ret = clk_prepare_enable(sdev->mclk);
		if (ret) {
			dev_err(sdev->dev, "Failed to enable mclk: %d\n", ret);
			goto err_clk;
		}
	}

	/* T4 Power to XCLR */
	if (sdev->delays.t4_pwr_to_xclr_ms)
		msleep(sdev->delays.t4_pwr_to_xclr_ms);

	/* Deassert Reset (XCLR High) */
	if (sdev->reset_gpio)
		gpiod_set_value_cansleep(sdev->reset_gpio, 1);

	/* T5 XCLR to I2C communication ready */
	if (sdev->delays.t5_xclr_to_i2c_ms)
		msleep(sdev->delays.t5_xclr_to_i2c_ms);

	sdev->is_powered_on = true;
	return 0;

err_clk:
	if (sdev->vaf)
		regulator_disable(sdev->vaf);
err_vaf:
	if (sdev->vif)
		regulator_disable(sdev->vif);
err_vif:
	if (sdev->vana2)
		regulator_disable(sdev->vana2);
err_vana2:
	if (sdev->vdig)
		regulator_disable(sdev->vdig);
err_vdig:
	if (sdev->vana1)
		regulator_disable(sdev->vana1);
	return ret;
}

static void lwis_sensor_power_off(struct lwis_sensor_dev *sdev)
{
	if (!sdev->is_powered_on)
		return;

	if (sdev->reset_gpio)
		gpiod_set_value_cansleep(sdev->reset_gpio, 0);

	if (sdev->mclk)
		clk_disable_unprepare(sdev->mclk);

	if (sdev->vaf)
		regulator_disable(sdev->vaf);
	if (sdev->vif)
		regulator_disable(sdev->vif);
	if (sdev->vana2)
		regulator_disable(sdev->vana2);
	if (sdev->vdig)
		regulator_disable(sdev->vdig);
	if (sdev->vana1)
		regulator_disable(sdev->vana1);

	sdev->is_powered_on = false;
}

static int lwis_sensor_parse_dt(struct lwis_sensor_dev *sdev,
				struct device_node *np)
{
	struct device *dev = sdev->dev;
	u32 delays[6];
	int ret;

	/* I2C Address */
	if (of_property_read_u32(np, "reg", &sdev->i2c_addr)) {
		dev_err(dev, "Missing 'reg' property for I2C address\n");
		return -EINVAL;
	}

	/* Clock Frequency */
	if (of_property_read_u32(np, "clock-frequency", &sdev->mclk_freq_hz))
		sdev->mclk_freq_hz = 25000000; /* Default 25 MHz */

	/* Regulators */
	sdev->vana1 = devm_regulator_get_optional(dev, "vana1");
	if (IS_ERR(sdev->vana1))
		sdev->vana1 = NULL;

	sdev->vana2 = devm_regulator_get_optional(dev, "vana2");
	if (IS_ERR(sdev->vana2))
		sdev->vana2 = NULL;

	sdev->vdig = devm_regulator_get_optional(dev, "vdig");
	if (IS_ERR(sdev->vdig))
		sdev->vdig = NULL;

	sdev->vif = devm_regulator_get_optional(dev, "vif");
	if (IS_ERR(sdev->vif))
		sdev->vif = NULL;

	sdev->vaf = devm_regulator_get_optional(dev, "vaf");
	if (IS_ERR(sdev->vaf))
		sdev->vaf = NULL;

	/* Power Delays */
	ret = of_property_read_u32_array(np, "google,power-delays-ms", delays, 6);
	if (!ret) {
		sdev->delays.t_vana1_vdig_ms = delays[0];
		sdev->delays.t_vdig_vana2_ms = delays[1];
		sdev->delays.t_vana2_vif_ms = delays[2];
		sdev->delays.t4_pwr_to_xclr_ms = delays[3];
		sdev->delays.t5_xclr_to_i2c_ms = delays[4];
		sdev->delays.t6_xclr_to_stream_ms = delays[5];
	} else {
		/* Fallback standard timing */
		sdev->delays.t_vana1_vdig_ms = 1;
		sdev->delays.t_vdig_vana2_ms = 1;
		sdev->delays.t_vana2_vif_ms = 1;
		sdev->delays.t4_pwr_to_xclr_ms = 2;
		sdev->delays.t5_xclr_to_i2c_ms = 3;
		sdev->delays.t6_xclr_to_stream_ms = 11;
	}

	/* Slew Rate */
	if (of_property_read_u32(np, "google,max-slew-rate-mv-per-us",
				 &sdev->max_slew_rate_mv_per_us))
		sdev->max_slew_rate_mv_per_us = 100;

	/* GPIOs */
	sdev->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sdev->reset_gpio))
		sdev->reset_gpio = NULL;

	sdev->xvs_gpio = devm_gpiod_get_optional(dev, "xvs", GPIOD_IN);
	if (IS_ERR(sdev->xvs_gpio))
		sdev->xvs_gpio = NULL;

	/* Clock */
	sdev->mclk = devm_clk_get_optional(dev, "mclk");
	if (IS_ERR(sdev->mclk))
		sdev->mclk = NULL;

	return 0;
}

static int lwis_sensor_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct lwis_sensor_dev *sdev;
	const struct of_device_id *match;
	const char *dev_node_name;
	int ret, minor;

	if (!np) {
		dev_err(dev, "Device node not found\n");
		return -ENODEV;
	}

	match = of_match_device(pdev->dev.driver->of_match_table, dev);
	if (!match) {
		dev_err(dev, "No matching of_device_id found\n");
		return -EINVAL;
	}

	sdev = devm_kzalloc(dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->dev = dev;
	platform_set_drvdata(pdev, sdev);

	sdev->is_ichi = of_device_is_compatible(np, "google,lwis-sensor-ichi");
	dev_node_name = sdev->is_ichi ? DEV_NAME_ICHI : DEV_NAME_GENERIC;

	dev_info(dev, "found node %s (%s)\n",
		 np->name, match->compatible);

	ret = lwis_sensor_parse_dt(sdev, np);
	if (ret) {
		dev_err(dev, "Failed to parse device tree: %d\n", ret);
		return ret;
	}

	dev_info(dev, "regulators bound successfully\n");

	/* Create character device */
	minor = 0;
	sdev->dev_num = MKDEV(MAJOR(g_lwis_dev_num), minor);

	cdev_init(&sdev->cdev, &lwis_sensor_fops);
	sdev->cdev.owner = THIS_MODULE;

	ret = cdev_add(&sdev->cdev, sdev->dev_num, 1);
	if (ret) {
		dev_err(dev, "Failed to add cdev: %d\n", ret);
		return ret;
	}

	sdev->char_dev = device_create(g_lwis_class, dev, sdev->dev_num, sdev,
				       "%s", dev_node_name);
	if (IS_ERR(sdev->char_dev)) {
		ret = PTR_ERR(sdev->char_dev);
		dev_err(dev, "Failed to create character device /dev/%s: %d\n",
			dev_node_name, ret);
		cdev_del(&sdev->cdev);
		return ret;
	}

	dev_info(dev, "/dev/%s character device created major: %d, minor: %d\n",
		 dev_node_name, MAJOR(sdev->dev_num), MINOR(sdev->dev_num));

	return 0;
}

static int lwis_sensor_remove(struct platform_device *pdev)
{
	struct lwis_sensor_dev *sdev = platform_get_drvdata(pdev);

	if (sdev) {
		lwis_sensor_power_off(sdev);
		if (sdev->char_dev)
			device_destroy(g_lwis_class, sdev->dev_num);
		cdev_del(&sdev->cdev);
	}
	return 0;
}

static const struct of_device_id lwis_sensor_of_match[] = {
	{ .compatible = "google,lwis-sensor-ichi", .data = (void *)"ichi" },
	{ .compatible = "google,lwis-sensor", .data = (void *)"generic" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lwis_sensor_of_match);

static struct platform_driver lwis_sensor_platform_driver = {
	.probe = lwis_sensor_probe,
	.remove = lwis_sensor_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = lwis_sensor_of_match,
	},
};

static int __init lwis_sensor_driver_init(void)
{
	int ret;

	/* Register chrdev region */
	ret = register_chrdev_region(MKDEV(g_lwis_major, 0), 16, DRIVER_NAME);
	if (ret < 0) {
		ret = alloc_chrdev_region(&g_lwis_dev_num, 0, 16, DRIVER_NAME);
		if (ret < 0) {
			pr_err("lwis_sensor: failed to allocate chrdev region\n");
			return ret;
		}
	} else {
		g_lwis_dev_num = MKDEV(g_lwis_major, 0);
	}

	g_lwis_class = class_create(THIS_MODULE, "lwis_sensor");
	if (IS_ERR(g_lwis_class)) {
		pr_err("lwis_sensor: failed to create device class\n");
		unregister_chrdev_region(g_lwis_dev_num, 16);
		return PTR_ERR(g_lwis_class);
	}

	ret = platform_driver_register(&lwis_sensor_platform_driver);
	if (ret) {
		pr_err("lwis_sensor: failed to register platform driver\n");
		class_destroy(g_lwis_class);
		unregister_chrdev_region(g_lwis_dev_num, 16);
		return ret;
	}

	pr_info("lwis_sensor: module initialized successfully\n");
	return 0;
}

static void __exit lwis_sensor_driver_exit(void)
{
	platform_driver_unregister(&lwis_sensor_platform_driver);
	if (g_lwis_class)
		class_destroy(g_lwis_class);
	unregister_chrdev_region(g_lwis_dev_num, 16);
	pr_info("lwis_sensor: module unloaded\n");
}

module_init(lwis_sensor_driver_init);
module_exit(lwis_sensor_driver_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Google LWIS Sensor Driver for Sony IMX08B (ICHI)");
MODULE_LICENSE("GPL v2");
