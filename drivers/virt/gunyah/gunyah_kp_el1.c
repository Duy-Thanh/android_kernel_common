// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/arm-smccc.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/align.h>
#include <linux/sections.h>
#include <asm/fixmap.h>
#include <asm/memory.h>
#include "gunyah_kernel_protect.h"
#include <trace/hooks/gunyah_kernel_protect.h>

#define KP_UPDATE_ASSET_INFO_FUNC_ID 0x5F
#define GHKP_HVC_ENABLE_KP_PROTECTION						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,					\
			   ARM_SMCCC_SMC_64,					\
			   ARM_SMCCC_OWNER_VENDOR_HYP,				\
			   KP_UPDATE_ASSET_INFO_FUNC_ID)

struct kp_instance {
	u32 kp_enabled;
	u64 rx_protection_status;
	u64 ro_protection_status;
	u64 bss_protection_status;
	u64 data_protection_status;
};

static struct kp_instance kp_status;

#define ADD_SEG_INFO(_layout, _type, _start, _end)				\
	do {									\
		if (WARN_ON((_layout)->nr >= KP_REGION_MAX))			\
			break;							\
		phys_addr_t _pstart = ALIGN_DOWN(__pa_symbol(_start), PAGE_SIZE); \
		phys_addr_t kbase   = __pa_symbol(_stext);			\
		phys_addr_t _pend   = ALIGN(__pa_symbol(_end), PAGE_SIZE);	\
		(_layout)->segs[(_layout)->nr].type   = _type;			\
		(_layout)->segs[(_layout)->nr].offset = (u64)(_pstart - kbase); \
		(_layout)->segs[(_layout)->nr].size   =				\
			ALIGN((u64)(_pend) - (_pstart), PAGE_SIZE);		\
		(_layout)->nr++;						\
	} while (0)

static void kp_protect_kernel_layout(void *ignored, const struct kp_layout_info *payload_res)
{
	struct arm_smccc_res res;

	memset(&res, 0, sizeof(res));
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
			  0, &res);
	pr_info("KP layout update HVC %lu:", res.a0);
#endif
}

static int __init kp_s2_freeze_late_init(void)
{
	static struct kp_segment_info segs[KP_REGION_MAX];
	struct kp_layout_info layout;

	memset(&kp_status, 0, sizeof(kp_status));

	layout.segs = segs;
	layout.nr   = 0;

	ADD_SEG_INFO(&layout, KP_REGION_TEXT,    _stext,        _etext);
	ADD_SEG_INFO(&layout, KP_REGION_RODATA,  __start_rodata, __end_rodata);
	ADD_SEG_INFO(&layout, KP_REGION_DATA,    _sdata,        _edata);
	ADD_SEG_INFO(&layout, KP_REGION_BSS,     __bss_start,   __bss_stop);

	if (!WARN_ON(layout.nr >= KP_REGION_MAX)) {
		segs[layout.nr].type   = KP_REGION_FIXMAP_TEXT_POKE0;
		segs[layout.nr].offset = (u64)__pa(fix_to_virt(FIX_TEXT_POKE0));
		segs[layout.nr].size   = PAGE_SIZE;
		layout.nr++;
	}

	trace_android_rvh_gh_freeze_kernel_protect(&layout);
	return 0;
}
late_initcall_sync(kp_s2_freeze_late_init);
