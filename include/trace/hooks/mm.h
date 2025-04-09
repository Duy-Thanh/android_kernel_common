/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <trace/hooks/vendor_hooks.h>
/*

DECLARE_RESTRICTED_HOOK(android_rvh_set_skip_swapcache_flags,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_gfp_zone_flags,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_readahead_gfp_mask,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);

*/
<<<<<<< PATCH SET (3d46a9a364bf272f946922990992780123ac2da8 ANDROID: mm: Add vendor hook in swapon())

DECLARE_HOOK(android_vh_adjust_swap_info_flags,
	TP_PROTO(unsigned long *flags),
	TP_ARGS(flags));

DECLARE_HOOK(android_vh_rmqueue_pcplist_override_batch,
	TP_PROTO(int *batch),
	TP_ARGS(batch));
||||||| BASE      (6430a1bc6edd1d5e65fb1a09d6b36996ecfce076 ANDROID: mm: Add vendor hook before rmqueue_bulk)

DECLARE_HOOK(android_vh_rmqueue_pcplist_override_batch,
	TP_PROTO(int *batch),
	TP_ARGS(batch));
=======
>>>>>>> BASE      (eb5e5c1f626e74ce56890b491edb90a1d11a7ea7 ANDROID: gki_defconfig: Enable Tegra SoCs)
#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
