/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM vmscan

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_VMSCAN_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_VMSCAN_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_RESTRICTED_HOOK(android_rvh_set_balance_anon_file_reclaim,
			TP_PROTO(bool *balance_anon_file_reclaim),
			TP_ARGS(balance_anon_file_reclaim), 1);
DECLARE_HOOK(android_vh_mglru_should_abort_scan,
	TP_PROTO(unsigned long nr_reclaimed, unsigned long nr_to_reclaim,
	unsigned int order, bool *bypass),
	TP_ARGS(nr_to_reclaim, nr_reclaimed, order, bypass));
DECLARE_HOOK(android_vh_tune_swappiness,
	TP_PROTO(int *swappiness),
	TP_ARGS(swappiness));
DECLARE_HOOK(android_vh_async_psi_bypass,
	TP_PROTO(bool *bypass),
	TP_ARGS(bypass));
<<<<<<< HEAD   (94052dc3881a826a00dfe079b0fad4c7965705ed Reapply "lsm: fix a missing security_uring_allowed() prototy)
||||||| BASE   (087f4434a20872505b3a7c50990b5cb5c9ac2154 BACKPORT: dma-fence: Add safe access helpers and document th)
DECLARE_HOOK(android_vh_use_vm_swappiness,
	TP_PROTO(bool *use_vm_swappiness),
	TP_ARGS(use_vm_swappiness));
DECLARE_HOOK(android_vh_tune_scan_control,
	TP_PROTO(bool *skip_swap),
	TP_ARGS(skip_swap));
=======
DECLARE_HOOK(android_vh_use_vm_swappiness,
	TP_PROTO(bool *use_vm_swappiness),
	TP_ARGS(use_vm_swappiness));
DECLARE_HOOK(android_vh_tune_scan_control,
	TP_PROTO(bool *skip_swap),
	TP_ARGS(skip_swap));
DECLARE_HOOK(android_vh_handle_folio_writeback,
	TP_PROTO(struct folio *folio, bool *bypass),
	TP_ARGS(folio, bypass));
DECLARE_HOOK(android_vh_reclaim_before_kswapd,
	TP_PROTO(unsigned long *nr_reclaimed),
	TP_ARGS(nr_reclaimed));
>>>>>>> CHANGE (c6c58a1140a9b512156beb0355ac9414a6253c70 ANDROID: vendor_hook: Add hooks to maintain folios that are )
DECLARE_HOOK(android_vh_page_referenced_check_bypass,
	TP_PROTO(struct folio *folio, unsigned long nr_to_scan, int lru, bool *bypass),
	TP_ARGS(folio, nr_to_scan, lru, bypass));
enum scan_balance;
DECLARE_HOOK(android_vh_tune_scan_type,
	TP_PROTO(enum scan_balance *scan_type),
	TP_ARGS(scan_type));
DECLARE_HOOK(android_vh_shrink_slab_bypass,
	TP_PROTO(gfp_t gfp_mask, int nid, struct mem_cgroup *memcg, int priority, bool *bypass),
	TP_ARGS(gfp_mask, nid, memcg, priority, bypass));
DECLARE_HOOK(android_vh_check_folio_look_around_ref,
	TP_PROTO(struct folio *folio, int *skip),
	TP_ARGS(folio, skip));
DECLARE_HOOK(android_vh_should_memcg_bypass,
	TP_PROTO(struct mem_cgroup *memcg, int priority, bool *bypass),
	TP_ARGS(memcg, priority, bypass));
#endif /* _TRACE_HOOK_VMSCAN_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
