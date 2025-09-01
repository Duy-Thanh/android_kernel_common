/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright 2024 NXP
 */

#ifndef _SCMI_IMX_H
#define _SCMI_IMX_H

#include <linux/bitfield.h>
#include <linux/errno.h>
#include <linux/types.h>

#define SCMI_IMX_CTRL_PDM_CLK_SEL	0	/* AON PDM clock sel */
#define SCMI_IMX_CTRL_MQS1_SETTINGS	1	/* AON MQS settings */
#define SCMI_IMX_CTRL_SAI1_MCLK		2	/* AON SAI1 MCLK */
#define SCMI_IMX_CTRL_SAI3_MCLK		3	/* WAKE SAI3 MCLK */
#define SCMI_IMX_CTRL_SAI4_MCLK		4	/* WAKE SAI4 MCLK */
#define SCMI_IMX_CTRL_SAI5_MCLK		5	/* WAKE SAI5 MCLK */

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#if IS_ENABLED(CONFIG_IMX_SCMI_MISC_DRV)
int scmi_imx_misc_ctrl_get(u32 id, u32 *num, u32 *val);
int scmi_imx_misc_ctrl_set(u32 id, u32 val);
#else
static inline int scmi_imx_misc_ctrl_get(u32 id, u32 *num, u32 *val)
{
	return -EOPNOTSUPP;
}

static inline int scmi_imx_misc_ctrl_set(u32 id, u32 val)
{
	return -EOPNOTSUPP;
}
#endif
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
int scmi_imx_misc_ctrl_get(u32 id, u32 *num, u32 *val);
int scmi_imx_misc_ctrl_set(u32 id, u32 val);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

#endif
