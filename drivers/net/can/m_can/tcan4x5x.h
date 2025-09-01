/* SPDX-License-Identifier: GPL-2.0
 *
 * tcan4x5x - Texas Instruments TCAN4x5x Family CAN controller driver
 *
 * Copyright (c) 2020 Pengutronix,
 *                    Marc Kleine-Budde <kernel@pengutronix.de>
 */

#ifndef _TCAN4X5X_H
#define _TCAN4X5X_H

#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

#include "m_can.h"

#define TCAN4X5X_SANITIZE_SPI 1

struct __packed tcan4x5x_buf_cmd {
	u8 cmd;
	__be16 addr;
	u8 len;
};

struct tcan4x5x_map_buf {
	struct tcan4x5x_buf_cmd cmd;
	u8 data[256 * sizeof(u32)];
} ____cacheline_aligned;

struct tcan4x5x_priv {
	struct m_can_classdev cdev;

	struct regmap *regmap;
	struct spi_device *spi;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *device_wake_gpio;
	struct gpio_desc *device_state_gpio;
	struct regulator *power;

	struct tcan4x5x_map_buf map_buf_rx;
	struct tcan4x5x_map_buf map_buf_tx;
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)

	bool nwkrq_voltage_vio;
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
};

static inline void
tcan4x5x_spi_cmd_set_len(struct tcan4x5x_buf_cmd *cmd, u8 len)
{
	/* number of u32 */
	cmd->len = len >> 2;
}

int tcan4x5x_regmap_init(struct tcan4x5x_priv *priv);

#endif
