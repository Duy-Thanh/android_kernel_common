/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * STMicroelectronics uvis25 sensor driver
 *
 * Copyright 2017 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 */

#ifndef ST_UVIS25_H
#define ST_UVIS25_H

#define ST_UVIS25_DEV_NAME		"uvis25"

#include <linux/iio/iio.h>

/**
 * struct st_uvis25_hw - ST UVIS25 sensor instance
 * @regmap: Register map of the device.
 * @trig: The trigger in use by the driver.
 * @enabled: Status of the sensor (false->off, true->on).
 * @irq: Device interrupt line (I2C or SPI).
 */
struct st_uvis25_hw {
	struct regmap *regmap;

	struct iio_trigger *trig;
	bool enabled;
	int irq;
	/* Ensure timestamp is naturally aligned */
	struct {
		u8 chan;
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
		aligned_s64 ts;
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
		s64 ts __aligned(8);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
	} scan;
};

extern const struct dev_pm_ops st_uvis25_pm_ops;

int st_uvis25_probe(struct device *dev, int irq, struct regmap *regmap);

#endif /* ST_UVIS25_H */
