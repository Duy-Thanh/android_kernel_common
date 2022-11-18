/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_LKL_KASAN_H
#define _ASM_LKL_KASAN_H

#ifdef CONFIG_KASAN
#include <linux/const.h>
#define KASAN_SHADOW_OFFSET _AC(CONFIG_KASAN_SHADOW_OFFSET, UL)
#define KASAN_SHADOW_SIZE 	_AC(CONFIG_KASAN_SHADOW_SIZE, UL)

#define LKL_IMG_SIZE 0x8000000

#define KASAN_SHADOW_SCALE_SHIFT 3

#define KASAN_SHADOW_START	KASAN_SHADOW_OFFSET
#define KASAN_SHADOW_END	KASAN_SHADOW_START + KASAN_SHADOW_SIZE

void __init kasan_early_init(void);
void __init kasan_init(void);

#else
static inline void kasan_early_init(void) { }
static inline void kasan_init(void) { }
#endif

#endif
