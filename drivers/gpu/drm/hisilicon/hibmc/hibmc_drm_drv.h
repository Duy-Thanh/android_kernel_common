/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Hisilicon Hibmc SoC drm driver
 *
 * Based on the bochs drm driver.
 *
 * Copyright (c) 2016 Huawei Limited.
 *
 * Author:
 *	Rongrong Zou <zourongrong@huawei.com>
 *	Rongrong Zou <zourongrong@gmail.com>
 *	Jianhua Li <lijianhua@huawei.com>
 */

#ifndef HIBMC_DRM_DRV_H
#define HIBMC_DRM_DRV_H

#include <linux/gpio/consumer.h>
#include <linux/i2c-algo-bit.h>
#include <linux/i2c.h>

#include <drm/drm_framebuffer.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
struct hibmc_vdac {
	struct drm_device *dev;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data bit_data;
};

struct hibmc_drm_private {
	/* hw */
	void __iomem   *mmio;

	/* drm */
	struct drm_device dev;
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct hibmc_vdac vdac;
};

static inline struct hibmc_vdac *to_hibmc_vdac(struct drm_connector *connector)
{
	return container_of(connector, struct hibmc_vdac, connector);
}

static inline struct hibmc_drm_private *to_hibmc_drm_private(struct drm_device *dev)
{
	return container_of(dev, struct hibmc_drm_private, dev);
}

void hibmc_set_power_mode(struct hibmc_drm_private *priv,
			  u32 power_mode);
void hibmc_set_current_gate(struct hibmc_drm_private *priv,
			    u32 gate);

int hibmc_de_init(struct hibmc_drm_private *priv);
int hibmc_vdac_init(struct hibmc_drm_private *priv);

int hibmc_ddc_create(struct drm_device *drm_dev, struct hibmc_vdac *connector);
void hibmc_ddc_del(struct hibmc_vdac *vdac);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
struct hibmc_connector {
	struct drm_connector base;

	struct i2c_adapter adapter;
	struct i2c_algo_bit_data bit_data;
};

struct hibmc_drm_private {
	/* hw */
	void __iomem   *mmio;

	/* drm */
	struct drm_device dev;
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct hibmc_connector connector;
};

static inline struct hibmc_connector *to_hibmc_connector(struct drm_connector *connector)
{
	return container_of(connector, struct hibmc_connector, base);
}

static inline struct hibmc_drm_private *to_hibmc_drm_private(struct drm_device *dev)
{
	return container_of(dev, struct hibmc_drm_private, dev);
}

void hibmc_set_power_mode(struct hibmc_drm_private *priv,
			  u32 power_mode);
void hibmc_set_current_gate(struct hibmc_drm_private *priv,
			    u32 gate);

int hibmc_de_init(struct hibmc_drm_private *priv);
int hibmc_vdac_init(struct hibmc_drm_private *priv);

int hibmc_ddc_create(struct drm_device *drm_dev, struct hibmc_connector *connector);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

#endif
