/* SPDX-License-Identifier: GPL-2.0 */
//
// ALSA SoC Texas Instruments TAS2781 Audio Smart Amplifier
//
// Copyright (C) 2022 - 2024 Texas Instruments Incorporated
// https://www.ti.com
//
// The TAS2781 driver implements a flexible and configurable
// algo coefficient setting for one, two, or even multiple
// TAS2781 chips.
//
// Author: Shenghao Ding <shenghao-ding@ti.com>
//

#ifndef __TAS2781_TLV_H__
#define __TAS2781_TLV_H__

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
static const __maybe_unused DECLARE_TLV_DB_SCALE(dvc_tlv, -10000, 50, 0);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
static const __maybe_unused DECLARE_TLV_DB_SCALE(dvc_tlv, -10000, 100, 0);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
static const __maybe_unused DECLARE_TLV_DB_SCALE(amp_vol_tlv, 1100, 50, 0);

#endif
