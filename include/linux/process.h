/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PROCESS_H
#define _LINUX_PROCESS_H
#include <linux/mm.h>

/*
 * mseal system mappings of process (user space).
 */
static inline unsigned long mseal_system_mappings(void)
{
	return VM_SEALED;
}

#endif

