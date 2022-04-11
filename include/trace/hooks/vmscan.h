/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmscan

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VMSCAN_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_tune_scan_type,
	TP_PROTO(char *scan_type),
	TP_ARGS(scan_type));
DECLARE_HOOK(android_vh_tune_swappiness,
	TP_PROTO(int *swappiness),
	TP_ARGS(swappiness));
DECLARE_HOOK(android_vh_shrink_slab_bypass,
	TP_PROTO(gfp_t gfp_mask, int nid, struct mem_cgroup *memcg, int priority, bool *bypass),
	TP_ARGS(gfp_mask, nid, memcg, priority, bypass));
DECLARE_HOOK(android_vh_tune_inactive_ratio,
	TP_PROTO(unsigned long *inactive_ratio, int file),
	TP_ARGS(inactive_ratio, file))
DECLARE_HOOK(android_vh_do_shrink_slab,
	TP_PROTO(struct shrinker *shrinker, struct shrink_control *shrinkctl, int priority),
	TP_ARGS(shrinker, shrinkctl, priority));
DECLARE_RESTRICTED_HOOK(android_rvh_set_balance_anon_file_reclaim,
			TP_PROTO(bool *balance_anon_file_reclaim),
			TP_ARGS(balance_anon_file_reclaim), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_inactive_is_low,
	TP_PROTO(int *ret, unsigned long gb, unsigned long *inactive_ratio, enum lru_list inactive_lru),
	TP_ARGS(ret, gb, inactive_ratio, inactive_lru), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_page_check_references,
	TP_PROTO(u64 *oem_data1, unsigned long vm_flags),
	TP_ARGS(oem_data1, vm_flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_snapshot_refaults,
	TP_PROTO(struct lruvec *target_lruvec, unsigned long refaults),
	TP_ARGS(target_lruvec, refaults), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_shrink_page_list,
	TP_PROTO(struct page *page, u64 hotness, bool swap_slot_cache_enabled, int *ret),
	TP_ARGS(page, hotness, swap_slot_cache_enabled, ret), 1);
#endif /* _TRACE_HOOK_VMSCAN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
