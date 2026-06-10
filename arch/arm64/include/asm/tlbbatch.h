/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_ARM64_TLBBATCH_H
#define _ARCH_ARM64_TLBBATCH_H

#include <linux/android_kabi.h>

struct arch_tlbflush_unmap_batch {
	/*
	 * For arm64, HW can do TLB shootdown, so we don't need to record a
	 * cpumask for sending IPIs.
	 */
	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
};

#endif /* _ARCH_ARM64_TLBBATCH_H */
