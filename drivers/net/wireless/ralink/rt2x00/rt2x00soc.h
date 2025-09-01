/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

 */

/*
	Module: rt2x00soc
	Abstract: Data structures for the rt2x00soc module.
 */

#ifndef RT2X00SOC_H
#define RT2X00SOC_H

/*
 * SoC driver handlers.
 */
int rt2x00soc_probe(struct platform_device *pdev, const struct rt2x00_ops *ops);
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
void rt2x00soc_remove(struct platform_device *pdev);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
int rt2x00soc_remove(struct platform_device *pdev);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
#ifdef CONFIG_PM
int rt2x00soc_suspend(struct platform_device *pdev, pm_message_t state);
int rt2x00soc_resume(struct platform_device *pdev);
#else
#define rt2x00soc_suspend	NULL
#define rt2x00soc_resume	NULL
#endif /* CONFIG_PM */

#endif /* RT2X00SOC_H */
