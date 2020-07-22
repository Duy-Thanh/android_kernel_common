/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSOCLOCKSOURCE_H
#define __ASM_VDSOCLOCKSOURCE_H

<<<<<<< HEAD   (bd3a00 ANDROID: GKI: update abi symbol lists now that CONFIG_DWC3 i)
struct arch_clocksource_data {
	bool vdso_direct;	/* Usable for direct VDSO access? */
=======
enum vdso_arch_clockmode {
	/* vdso clocksource not usable */
	VDSO_CLOCKMODE_NONE,
	/* vdso clocksource for both 32 and 64bit tasks */
	VDSO_CLOCKMODE_ARCHTIMER,
	/* vdso clocksource for 64bit tasks only */
	VDSO_CLOCKMODE_ARCHTIMER_NOCOMPAT,
>>>>>>> BRANCH (d811d2 Linux 5.4.53)
};

#endif
