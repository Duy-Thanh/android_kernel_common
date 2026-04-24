/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _GUNYAH_KERNEL_PROTECT_H
#define _GUNYAH_KERNEL_PROTECT_H

/**
 * Kernel Protect section taxonomy
 */
enum kp_region_type {
	KP_REGION_TEXT = 0,
	KP_REGION_RODATA = 1,
	KP_REGION_DATA = 2,
	KP_REGION_BSS = 3,
	KP_REGION_FIXMAP_TEXT_POKE0 = 4,
	KP_REGION_MAX,
};

/**
 * struct kp_segment_info - page-aligned physical segment descriptor
 * @type:   enum kp_region_type
 * @offset: PA offset from _stext (KASLR-adjusted)
 * @size:   bytes, page-aligned
 */
struct kp_segment_info {
	u64 type;
	u64 offset;
	u64 size;
};

/**
 * struct kp_layout_info - full kernel layout passed to EL2
 * @nr:   number of valid entries in @segs
 * @segs: array of segment descriptors
 */
struct kp_layout_info {
	u32 nr;
	struct kp_segment_info *segs;
};

/**
 * struct kp_instance - per-segment HVC return status
 * @kp_enabled:            set to 1 after all HVCs complete successfully
 * @rx_protection_status:  HVC return for TEXT and FIXMAP segments
 * @ro_protection_status:  HVC return for RODATA segment
 * @data_protection_status: HVC return for DATA segment
 * @bss_protection_status: HVC return for BSS segment
 */
struct kp_instance {
	u32 kp_enabled;
	u64 rx_protection_status;
	u64 ro_protection_status;
	u64 data_protection_status;
	u64 bss_protection_status;
};

#endif /* _GUNYAH_KERNEL_PROTECT_H */
