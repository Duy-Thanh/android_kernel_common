// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Arm Limited
 */

#define pr_fmt(fmt) "smccc: " fmt

#include <linux/cache.h>
#include <linux/init.h>
#include <linux/arm-smccc.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <asm/archrandom.h>

static u32 smccc_version = ARM_SMCCC_VERSION_1_0;
static enum arm_smccc_conduit smccc_conduit = SMCCC_CONDUIT_NONE;

bool __ro_after_init smccc_trng_available = false;
<<<<<<< HEAD   (0bc7ce Merge 4118bd1834a9 ("KVM: x86/mmu: Ensure that kvm_release_p)
u64 __ro_after_init smccc_has_sve_hint = false;
||||||| BASE
u64 __ro_after_init smccc_has_sve_hint = false;
s32 __ro_after_init smccc_soc_id_version = SMCCC_RET_NOT_SUPPORTED;
s32 __ro_after_init smccc_soc_id_revision = SMCCC_RET_NOT_SUPPORTED;
=======
s32 __ro_after_init smccc_soc_id_version = SMCCC_RET_NOT_SUPPORTED;
s32 __ro_after_init smccc_soc_id_revision = SMCCC_RET_NOT_SUPPORTED;
>>>>>>> BRANCH (52f863 Linux 6.1.120)

void __init arm_smccc_version_init(u32 version, enum arm_smccc_conduit conduit)
{
	smccc_version = version;
	smccc_conduit = conduit;

	smccc_trng_available = smccc_probe_trng();
<<<<<<< HEAD   (0bc7ce Merge 4118bd1834a9 ("KVM: x86/mmu: Ensure that kvm_release_p)
	if (IS_ENABLED(CONFIG_ARM64_SVE) &&
	    smccc_version >= ARM_SMCCC_VERSION_1_3)
		smccc_has_sve_hint = true;
||||||| BASE
	if (IS_ENABLED(CONFIG_ARM64_SVE) &&
	    smccc_version >= ARM_SMCCC_VERSION_1_3)
		smccc_has_sve_hint = true;

	if ((smccc_version >= ARM_SMCCC_VERSION_1_2) &&
	    (smccc_conduit != SMCCC_CONDUIT_NONE)) {
		arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
				     ARM_SMCCC_ARCH_SOC_ID, &res);
		if ((s32)res.a0 >= 0) {
			arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_SOC_ID, 0, &res);
			smccc_soc_id_version = (s32)res.a0;
			arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_SOC_ID, 1, &res);
			smccc_soc_id_revision = (s32)res.a0;
		}
	}
=======

	if ((smccc_version >= ARM_SMCCC_VERSION_1_2) &&
	    (smccc_conduit != SMCCC_CONDUIT_NONE)) {
		arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
				     ARM_SMCCC_ARCH_SOC_ID, &res);
		if ((s32)res.a0 >= 0) {
			arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_SOC_ID, 0, &res);
			smccc_soc_id_version = (s32)res.a0;
			arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_SOC_ID, 1, &res);
			smccc_soc_id_revision = (s32)res.a0;
		}
	}
>>>>>>> BRANCH (52f863 Linux 6.1.120)
}

enum arm_smccc_conduit arm_smccc_1_1_get_conduit(void)
{
	if (smccc_version < ARM_SMCCC_VERSION_1_1)
		return SMCCC_CONDUIT_NONE;

	return smccc_conduit;
}
EXPORT_SYMBOL_GPL(arm_smccc_1_1_get_conduit);

u32 arm_smccc_get_version(void)
{
	return smccc_version;
}
EXPORT_SYMBOL_GPL(arm_smccc_get_version);

static int __init smccc_devices_init(void)
{
	struct platform_device *pdev;

	if (smccc_trng_available) {
		pdev = platform_device_register_simple("smccc_trng", -1,
						       NULL, 0);
		if (IS_ERR(pdev))
			pr_err("smccc_trng: could not register device: %ld\n",
			       PTR_ERR(pdev));
	}

	return 0;
}
device_initcall(smccc_devices_init);
