/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_CLOCKSOURCE_H
#define _ASM_CLOCKSOURCE_H

#include <asm/vdso/clocksource.h>
<<<<<<< HEAD   (bd3a00 ANDROID: GKI: update abi symbol lists now that CONFIG_DWC3 i)
=======

struct arch_clocksource_data {
	/* Usable for direct VDSO access? */
	enum vdso_arch_clockmode clock_mode;
};
>>>>>>> BRANCH (d811d2 Linux 5.4.53)

#endif
