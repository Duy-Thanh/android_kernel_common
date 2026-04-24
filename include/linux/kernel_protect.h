/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KERNEL_PROTECT_H
#define _LINUX_KERNEL_PROTECT_H
/**
 * Kernel Protect section Taxonomy
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
 * struct kp_el1_layout collects final
 * page aligned segment layour.
 * (offsets already KASLR-adjusted)
 */
struct kp_segment_info {
	u64 type;		/* enum kp_region_type */
	u64 offset;		/* VA offset from _stext */
	u64 size;		/* bytes, page-aligned */
};

struct kp_layout_info {
	u32 nr;				/* number of regions */
	struct kp_segment_info *segs;	/* caller must ensure assignment here */
};

#endif /*_LINUX_KERNEL_PROTECT_H */

