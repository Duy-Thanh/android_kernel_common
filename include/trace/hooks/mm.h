/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <trace/hooks/vendor_hooks.h>

struct shmem_inode_info;
struct folio;

DECLARE_RESTRICTED_HOOK(android_rvh_shmem_get_folio,
			TP_PROTO(struct shmem_inode_info *info, struct folio **folio),
			TP_ARGS(info, folio), 2);

DECLARE_RESTRICTED_HOOK(android_rvh_set_gfp_zone_flags,
			TP_PROTO(unsigned int *flags),	/* gfp_t *flags */
			TP_ARGS(flags), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_readahead_gfp_mask,
			TP_PROTO(unsigned int *flags),	/* gfp_t *flags */
			TP_ARGS(flags), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_alloc_pages,
			TP_PROTO(gfp_t alloc_gfp, int order, int alloc_flags,
			int step, struct page **page),
			TP_ARGS(alloc_gfp, order, alloc_flags, step, page));

DECLARE_RESTRICTED_HOOK(android_rvh_free_pages,
			TP_PROTO(struct page **page, unsigned int order,
			unsigned long pfn, int migratetype, bool remove),
			TP_ARGS(page, order, pfn, migratetype, remove));

DECLARE_RESTRICTED_HOOK(android_rvh_buddy_rmqueue,
			TP_PROTO(int order, gfp_t gfp_mask, bool *pcp_bypass),
			TP_ARGS(order, gfp_mask, pcp_bypass));

DECLARE_RESTRICTED_HOOK(android_rvh_gfp_to_alloc_flags,
			TP_PROTO(gfp_t gfp_mask, int *should_alloc_harder),
			TP_ARGS(gfp_mask, should_alloc_harder));

DECLARE_RESTRICTED_HOOK(android_rvh_do_fault_around,
			TP_PROTO(unsigned long *nr_pages),
			TP_ARGS(nr_pages));

DECLARE_RESTRICTED_HOOK(android_rvh_si_mem_available,
			TP_PROTO(long *mem_available),
			TP_ARGS(mem_available));

#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
