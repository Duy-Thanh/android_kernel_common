/* SPDX-License-Identifier: GPL-2.0 */

#include <asm/hyp_image.h>
#include <asm/page-def.h>

SECTIONS {
	HYP_SECTION(.text)
	HYP_SECTION(.bss)
	HYP_SECTION(.rodata)
	HYP_SECTION(.data)
}
