// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/arm-smccc.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/align.h>
#include <linux/kernel_protect.h>
#include <trace/hooks/kernel_protect.h>

#define KP_PAGE_SIZE  4096
#define KP_UPDATE_ASSET_INFO_FUNC_ID 0x3FA0
#define GHKP_HVC_ENABLE_KP_PROTECTION						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,					\
			ARM_SMCCC_SMC_64,						\
			ARM_SMCCC_OWNER_VENDOR_HYP,					\
			KP_UPDATE_ASSET_INFO_FUNC_ID)

struct kp_instance {
	u32 kp_enabled;
	u64 rx_protection_status;
	u64 ro_protection_status;
	u64 bss_protection_status;
	u64 data_protection_status;
};

static struct kp_instance kp_status;

static void kp_protect_kernel_layout(void *ignored, const struct kp_layout_info *payload_res)
{
	struct arm_smccc_res res;

	memset((void *)&res, 0, sizeof(struct arm_smccc_res));
	pr_info("KP Callback invoked\n");
#ifndef KP_ENABLE_COMPACT_HVC

	arm_smccc_1_1_hvc(GHKP_HVC_ENABLE_KP_PROTECTION,
			  payload_res->segs[KP_REGION_FIXMAP_TEXT_POKE0].offset,
			  payload_res->segs[KP_REGION_FIXMAP_TEXT_POKE0].size,
			  payload_res->segs[KP_REGION_FIXMAP_TEXT_POKE0].type, &res);
	if (res.a0) {
		pr_info("KP layout update Fixmap %lu:", res.a0);
		kp_status.rx_protection_status = res.a0;
	}

	arm_smccc_1_1_hvc(GHKP_HVC_ENABLE_KP_PROTECTION,
			  payload_res->segs[KP_REGION_TEXT].offset,
			  payload_res->segs[KP_REGION_TEXT].size,
			  payload_res->segs[KP_REGION_TEXT].type, &res);
	if (res.a0) {
		pr_info("KP layout update TEXT HVC %lu:", res.a0);
		kp_status.rx_protection_status = res.a0;
	}

	arm_smccc_1_1_hvc(GHKP_HVC_ENABLE_KP_PROTECTION,
			  payload_res->segs[KP_REGION_RODATA].offset,
			  payload_res->segs[KP_REGION_RODATA].size,
			  payload_res->segs[KP_REGION_RODATA].type, &res);
	if (res.a0) {
		pr_info("KP layout update RODATA HVC %lu:", res.a0);
		kp_status.ro_protection_status = res.a0;
	}
	arm_smccc_1_1_hvc(GHKP_HVC_ENABLE_KP_PROTECTION,
			  payload_res->segs[KP_REGION_DATA].offset,
			  payload_res->segs[KP_REGION_DATA].size,
			  payload_res->segs[KP_REGION_DATA].type, &res);
	if (res.a0) {
		pr_info("KP layout update DATA HVC %lu:", res.a0);
		kp_status.data_protection_status = res.a0;
	}

	arm_smccc_1_1_hvc(GHKP_HVC_ENABLE_KP_PROTECTION,
			  payload_res->segs[KP_REGION_BSS].offset,
			  payload_res->segs[KP_REGION_BSS].size,
			  payload_res->segs[KP_REGION_BSS].type, &res);
	if (res.a0) {
		pr_info("KP layout update BSS HVC %lu:", res.a0);
		kp_status.bss_protection_status = res.a0;
	}
	pr_info("Success: KP layout update HVC %lu:", res.a0);
#else
	arm_smccc_1_1_hvc(GHKP_HVC_ENABLE_KP_PROTECTION,
			  virt_to_phys(payload_res), sizeof(*payload_res),
			  0,
			  &res);
	pr_info("KP layout update  HVC %lu:", res.a0);
#endif
}

static int __init kp_layout_info_init(void)
{
	int ret = 0;

	memset(&kp_status, 0, sizeof(kp_status));
	ret = register_trace_android_rvh_freeze_kernel_protect(kp_protect_kernel_layout,
							       NULL);

	if (ret != 0)
		pr_info("kp : layout result failed %u\n", ret);
	return ret;
}
device_initcall_sync(kp_layout_info_init);

