// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/arm-smccc.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/align.h>
#include <asm/sections.h>
#include <asm/fixmap.h>
#include <asm/memory.h>
#include "gunyah_kernel_protect.h"

#define KP_UPDATE_ASSET_INFO_FUNC_ID 0x5F
#define GHKP_HVC_ENABLE_KP_PROTECTION						\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL,					\
			   ARM_SMCCC_SMC_64,					\
			   ARM_SMCCC_OWNER_VENDOR_HYP,				\
			   KP_UPDATE_ASSET_INFO_FUNC_ID)

static struct kp_instance kp_status;

#define ADD_SEG_INFO(_layout, _type, _start, _end)				\
	do {									\
		if (WARN_ON((_layout)->nr >= KP_REGION_MAX))			\
			break;							\
		phys_addr_t _pstart = ALIGN_DOWN(__pa_symbol(_start), PAGE_SIZE); \
		phys_addr_t _pend   = ALIGN(__pa_symbol(_end), PAGE_SIZE);	\
		(_layout)->segs[(_layout)->nr].type   = _type;			\
		(_layout)->segs[(_layout)->nr].offset = (u64)_pstart;		\
		(_layout)->segs[(_layout)->nr].size   =				\
			ALIGN((u64)(_pend) - (_pstart), PAGE_SIZE);		\
		(_layout)->nr++;						\
	} while (0)

static void kp_send_hvc(const struct kp_layout_info *layout)
{
	struct arm_smccc_res res;
	int i;

	for (i = 0; i < layout->nr; i++) {
		memset(&res, 0, sizeof(res));
		arm_smccc_1_1_hvc(GHKP_HVC_ENABLE_KP_PROTECTION,
				  layout->segs[i].offset,
				  layout->segs[i].size,
				  layout->segs[i].type, &res);

		pr_info("KP layout update seg[%d] type=%llu ret=%lu\n",
			i, layout->segs[i].type, res.a0);

		switch (layout->segs[i].type) {
		case KP_REGION_TEXT:
		case KP_REGION_FIXMAP_TEXT_POKE0:
			kp_status.rx_protection_status = res.a0;
			break;
		case KP_REGION_RODATA:
			kp_status.ro_protection_status = res.a0;
			break;
		case KP_REGION_DATA:
			kp_status.data_protection_status = res.a0;
			break;
		case KP_REGION_BSS:
			kp_status.bss_protection_status = res.a0;
			break;
		default:
			break;
		}
	}
	pr_info("KP layout update complete\n");
}

static int __init kp_s2_freeze_late_init(void)
{
	static struct kp_segment_info segs[KP_REGION_MAX];
	struct kp_layout_info layout;

	memset(&kp_status, 0, sizeof(kp_status));

	layout.segs = segs;
	layout.nr   = 0;

	ADD_SEG_INFO(&layout, KP_REGION_TEXT,   _stext,         _etext);
	ADD_SEG_INFO(&layout, KP_REGION_RODATA, __start_rodata, __end_rodata);
	ADD_SEG_INFO(&layout, KP_REGION_DATA,   _sdata,         _edata);
	ADD_SEG_INFO(&layout, KP_REGION_BSS,    __bss_start,    __bss_stop);

	if (!WARN_ON(layout.nr >= KP_REGION_MAX)) {
		segs[layout.nr].type   = KP_REGION_FIXMAP_TEXT_POKE0;
		segs[layout.nr].offset = (u64)__pa(fix_to_virt(FIX_TEXT_POKE0));
		segs[layout.nr].size   = PAGE_SIZE;
		layout.nr++;
	}

	kp_send_hvc(&layout);

	kp_status.kp_enabled = 1;

	return 0;
}
late_initcall_sync(kp_s2_freeze_late_init);
