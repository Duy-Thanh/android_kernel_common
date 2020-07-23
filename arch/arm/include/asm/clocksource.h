/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_CLOCKSOURCE_H
#define _ASM_CLOCKSOURCE_H

<<<<<<< HEAD   (f51ce0 ANDROID: power: wakeup_reason: export log_threaded_irq_wakeu)
#include <asm/vdso/clocksource.h>
=======
enum vdso_arch_clockmode {
	/* vdso clocksource not usable */
	VDSO_CLOCKMODE_NONE,
	/* vdso clocksource usable */
	VDSO_CLOCKMODE_ARCHTIMER,
	VDSO_CLOCKMODE_ARCHTIMER_NOCOMPAT = VDSO_CLOCKMODE_ARCHTIMER,
};

struct arch_clocksource_data {
	/* Usable for direct VDSO access? */
	enum vdso_arch_clockmode clock_mode;
};
>>>>>>> BRANCH (d811d2 Linux 5.4.53)

#endif /* _ASM_CLOCKSOURCE_H */
