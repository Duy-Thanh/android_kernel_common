/* SPDX-License-Identifier: GPL-2.0-only */

#include <asm/kvm_hypevents.h>

#include <linux/arm-smccc.h>

#undef arm_smccc_1_1_smc
#define arm_smccc_1_1_smc(...)					\
	do {							\
		__hyp_exit();					\
		__arm_smccc_1_1(SMCCC_SMC_INST, __VA_ARGS__);	\
		__hyp_enter();					\
	} while (0)

/*
 * arm_smccc_1_1_smc is a macro around __arm_smccc_1_1 but arm_smccc_1_2_smc is
 * a function so we cannot follow the pattern used to wrap arm_smccc_1_1_smc.
 * Hence the nvhe prefix.
 */
#define nvhe_arm_smccc_1_2_smc(args, regs)                     \
       do {                                                    \
               __hyp_exit();                                   \
               arm_smccc_1_2_smc(args, regs);                  \
	       __hyp_enter();                                  \
       } while (0)
