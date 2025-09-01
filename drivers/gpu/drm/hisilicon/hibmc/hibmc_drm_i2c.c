// SPDX-License-Identifier: GPL-2.0-or-later
/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *      Tian Tao <tiantao6@hisilicon.com>
 */

#include <linux/delay.h>
#include <linux/pci.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>

#include "hibmc_drm_drv.h"

#define GPIO_DATA		0x0802A0
#define GPIO_DATA_DIRECTION	0x0802A4

#define I2C_SCL_MASK		BIT(0)
#define I2C_SDA_MASK		BIT(1)

static void hibmc_set_i2c_signal(void *data, u32 mask, int value)
{
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
	struct hibmc_vdac *vdac = data;
	struct hibmc_drm_private *priv = to_hibmc_drm_private(vdac->connector.dev);
	u32 tmp_dir = readl(priv->mmio + GPIO_DATA_DIRECTION);

	if (value) {
		tmp_dir &= ~mask;
		writel(tmp_dir, priv->mmio + GPIO_DATA_DIRECTION);
	} else {
		u32 tmp_data = readl(priv->mmio + GPIO_DATA);

		tmp_data &= ~mask;
		writel(tmp_data, priv->mmio + GPIO_DATA);

		tmp_dir |= mask;
		writel(tmp_dir, priv->mmio + GPIO_DATA_DIRECTION);
	}
}

static int hibmc_get_i2c_signal(void *data, u32 mask)
{
	struct hibmc_vdac *vdac = data;
	struct hibmc_drm_private *priv = to_hibmc_drm_private(vdac->connector.dev);
	u32 tmp_dir = readl(priv->mmio + GPIO_DATA_DIRECTION);

	if ((tmp_dir & mask) != mask) {
		tmp_dir &= ~mask;
		writel(tmp_dir, priv->mmio + GPIO_DATA_DIRECTION);
	}

	return (readl(priv->mmio + GPIO_DATA) & mask) ? 1 : 0;
}

static void hibmc_ddc_setsda(void *data, int state)
{
	hibmc_set_i2c_signal(data, I2C_SDA_MASK, state);
}

static void hibmc_ddc_setscl(void *data, int state)
{
	hibmc_set_i2c_signal(data, I2C_SCL_MASK, state);
}

static int hibmc_ddc_getsda(void *data)
{
	return hibmc_get_i2c_signal(data, I2C_SDA_MASK);
}

static int hibmc_ddc_getscl(void *data)
{
	return hibmc_get_i2c_signal(data, I2C_SCL_MASK);
}

int hibmc_ddc_create(struct drm_device *drm_dev, struct hibmc_vdac *vdac)
{
	vdac->adapter.owner = THIS_MODULE;
	snprintf(vdac->adapter.name, I2C_NAME_SIZE, "HIS i2c bit bus");
	vdac->adapter.dev.parent = drm_dev->dev;
	i2c_set_adapdata(&vdac->adapter, vdac);
	vdac->adapter.algo_data = &vdac->bit_data;

	vdac->bit_data.udelay = 20;
	vdac->bit_data.timeout = usecs_to_jiffies(2000);
	vdac->bit_data.data = vdac;
	vdac->bit_data.setsda = hibmc_ddc_setsda;
	vdac->bit_data.setscl = hibmc_ddc_setscl;
	vdac->bit_data.getsda = hibmc_ddc_getsda;
	vdac->bit_data.getscl = hibmc_ddc_getscl;

	return i2c_bit_add_bus(&vdac->adapter);
}

void hibmc_ddc_del(struct hibmc_vdac *vdac)
{
	i2c_del_adapter(&vdac->adapter);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
	struct hibmc_connector *hibmc_connector = data;
	struct hibmc_drm_private *priv = to_hibmc_drm_private(hibmc_connector->base.dev);
	u32 tmp_dir = readl(priv->mmio + GPIO_DATA_DIRECTION);

	if (value) {
		tmp_dir &= ~mask;
		writel(tmp_dir, priv->mmio + GPIO_DATA_DIRECTION);
	} else {
		u32 tmp_data = readl(priv->mmio + GPIO_DATA);

		tmp_data &= ~mask;
		writel(tmp_data, priv->mmio + GPIO_DATA);

		tmp_dir |= mask;
		writel(tmp_dir, priv->mmio + GPIO_DATA_DIRECTION);
	}
}

static int hibmc_get_i2c_signal(void *data, u32 mask)
{
	struct hibmc_connector *hibmc_connector = data;
	struct hibmc_drm_private *priv = to_hibmc_drm_private(hibmc_connector->base.dev);
	u32 tmp_dir = readl(priv->mmio + GPIO_DATA_DIRECTION);

	if ((tmp_dir & mask) != mask) {
		tmp_dir &= ~mask;
		writel(tmp_dir, priv->mmio + GPIO_DATA_DIRECTION);
	}

	return (readl(priv->mmio + GPIO_DATA) & mask) ? 1 : 0;
}

static void hibmc_ddc_setsda(void *data, int state)
{
	hibmc_set_i2c_signal(data, I2C_SDA_MASK, state);
}

static void hibmc_ddc_setscl(void *data, int state)
{
	hibmc_set_i2c_signal(data, I2C_SCL_MASK, state);
}

static int hibmc_ddc_getsda(void *data)
{
	return hibmc_get_i2c_signal(data, I2C_SDA_MASK);
}

static int hibmc_ddc_getscl(void *data)
{
	return hibmc_get_i2c_signal(data, I2C_SCL_MASK);
}

int hibmc_ddc_create(struct drm_device *drm_dev,
		     struct hibmc_connector *connector)
{
	connector->adapter.owner = THIS_MODULE;
	snprintf(connector->adapter.name, I2C_NAME_SIZE, "HIS i2c bit bus");
	connector->adapter.dev.parent = drm_dev->dev;
	i2c_set_adapdata(&connector->adapter, connector);
	connector->adapter.algo_data = &connector->bit_data;

	connector->bit_data.udelay = 20;
	connector->bit_data.timeout = usecs_to_jiffies(2000);
	connector->bit_data.data = connector;
	connector->bit_data.setsda = hibmc_ddc_setsda;
	connector->bit_data.setscl = hibmc_ddc_setscl;
	connector->bit_data.getsda = hibmc_ddc_getsda;
	connector->bit_data.getscl = hibmc_ddc_getscl;

	return i2c_bit_add_bus(&connector->adapter);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
}
