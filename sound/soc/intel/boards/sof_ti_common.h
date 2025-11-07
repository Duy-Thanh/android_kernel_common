/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2025 Intel Corporation.
 */

/*
 * This file defines data structures used in Machine Driver for Intel
 * platforms with TI Codecs.
 */
#ifndef __SOF_TI_COMMON_H
#define __SOF_TI_COMMON_H

#include <sound/soc.h>
#include <sound/soc-acpi-intel-ssp-common.h>

/*
 * TI TAS2563
 */
#define TAS2563_CODEC_DAI	"tasdev_codec"
#define TAS2563_DEV0_NAME	"i2c-" TAS2563_ACPI_HID ":00"

void sof_tas2563_dai_link(struct device *dev, struct snd_soc_dai_link *link);

#endif /* __SOF_TI_COMMON_H */
