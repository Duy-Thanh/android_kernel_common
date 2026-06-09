/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_ARM64_TLBBATCH_H
#define _ARCH_ARM64_TLBBATCH_H

#include <linux/cpumask.h>
#include <linux/android_kabi.h>
#include <linux/cpumask.h>

struct arch_tlbflush_unmap_batch {
#ifdef CONFIG_ARM64_ERRATUM_4193714
	/*
	 * Track CPUs that need SME DVMSync on completion of this batch.
	 * Otherwise, the arm64 HW can do tlb shootdown, so we don't need to
	 * record cpumask for sending IPI
	 */
	cpumask_var_t cpumask;
<<<<<<< HEAD   (e99aab26ecd1f3967045b8c07aa8e13e301ed770 Merge c6c87a23de4b ("arm64: errata: Work around early CME DV)
#else
||||||| BASE   (42c0dddfeb0769ff388e9244b1edf1482d7b4358 ANDROID: GKI: vivo add symbols to symbol list)
=======
#endif
>>>>>>> BRANCH (e932dac5a9a775d515ec0def692f9e3e974cb3e2 ANDROID: GKI: update symbol list for galaxy)
	ANDROID_KABI_RESERVE(1);
#endif
};

#endif /* _ARCH_ARM64_TLBBATCH_H */
