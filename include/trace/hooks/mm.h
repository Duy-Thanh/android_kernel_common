/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <trace/hooks/vendor_hooks.h>

struct shmem_inode_info;
struct folio;
struct folio_batch;

DECLARE_RESTRICTED_HOOK(android_rvh_shmem_get_folio,
			TP_PROTO(struct shmem_inode_info *info, struct folio **folio),
			TP_ARGS(info, folio), 2);

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
struct mem_cgroup;
DECLARE_HOOK(android_vh_mem_cgroup_alloc,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_mem_cgroup_free,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_io_statistics,
	TP_PROTO(struct address_space *mapping, unsigned int index,
		unsigned int nr_page, bool read, bool direct),
	TP_ARGS(mapping, index, nr_page, read, direct));

struct cma;
DECLARE_HOOK(android_vh_cma_alloc_bypass,
	TP_PROTO(struct cma *cma, unsigned long count, unsigned int align,
		gfp_t gfp_mask, struct page **page, bool *bypass),
	TP_ARGS(cma, count, align, gfp_mask, page, bypass));

struct compact_control;
DECLARE_HOOK(android_vh_isolate_freepages,
	TP_PROTO(struct compact_control *cc, struct page *page, bool *bypass),
	TP_ARGS(cc, page, bypass));

struct oom_control;
DECLARE_HOOK(android_vh_oom_check_panic,
	TP_PROTO(struct oom_control *oc, int *ret),
	TP_ARGS(oc, ret));

struct page_vma_mapped_walk;
DECLARE_HOOK(android_vh_slab_alloc_node,
	TP_PROTO(void *object, unsigned long addr, struct kmem_cache *s),
	TP_ARGS(object, addr, s));
DECLARE_HOOK(android_vh_slab_free,
	TP_PROTO(unsigned long addr, struct kmem_cache *s),
	TP_ARGS(addr, s));
DECLARE_HOOK(android_vh_test_clear_look_around_ref,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_look_around_migrate_folio,
	TP_PROTO(struct folio *old_folio, struct folio *new_folio),
	TP_ARGS(old_folio, new_folio));
DECLARE_HOOK(android_vh_look_around,
	TP_PROTO(struct page_vma_mapped_walk *pvmw, struct folio *folio,
		struct vm_area_struct *vma, int *referenced),
	TP_ARGS(pvmw, folio, vma, referenced));
<<<<<<< HEAD   (c79ee7 ANDROID: Load cc_library from rules_cc.)

DECLARE_HOOK(android_vh_calc_alloc_flags,
	TP_PROTO(gfp_t gfp_mask, unsigned int *alloc_flags,
||||||| BASE
DECLARE_HOOK(android_vh_free_unref_page_bypass,
	TP_PROTO(struct page *page, int order, int migratetype, bool *bypass),
	TP_ARGS(page, order, migratetype, bypass));
DECLARE_HOOK(android_vh_kvmalloc_node_use_vmalloc,
	TP_PROTO(size_t size, gfp_t *kmalloc_flags, bool *use_vmalloc),
	TP_ARGS(size, kmalloc_flags, use_vmalloc));
DECLARE_HOOK(android_vh_should_alloc_pages_retry,
	TP_PROTO(gfp_t gfp_mask, int order, int *alloc_flags,
	int migratetype, struct zone *preferred_zone, struct page **page, bool *should_alloc_retry),
	TP_ARGS(gfp_mask, order, alloc_flags,
		migratetype, preferred_zone, page, should_alloc_retry));
DECLARE_HOOK(android_vh_alloc_pages_adjust_wmark,
	TP_PROTO(gfp_t gfp_mask, int order, int *alloc_flags),
	TP_ARGS(gfp_mask, order, alloc_flags));
DECLARE_HOOK(android_vh_alloc_pages_reset_wmark,
	TP_PROTO(gfp_t gfp_mask, int order, int *alloc_flags,
	unsigned long *did_some_progress, int *no_progress_loops,
	unsigned long direct_reclaim_retries),
	TP_ARGS(gfp_mask, order, alloc_flags, did_some_progress,
	no_progress_loops, direct_reclaim_retries));
DECLARE_HOOK(android_vh_unreserve_highatomic_bypass,
	TP_PROTO(bool force, struct zone *zone, bool *skip_unreserve_highatomic),
	TP_ARGS(force, zone, skip_unreserve_highatomic));
DECLARE_HOOK(android_vh_rmqueue_bulk_bypass,
	TP_PROTO(unsigned int order, struct per_cpu_pages *pcp, int migratetype,
		struct list_head *list),
	TP_ARGS(order, pcp, migratetype, list));
DECLARE_HOOK(android_vh_ra_tuning_max_page,
	TP_PROTO(struct readahead_control *ractl, unsigned long *max_page),
	TP_ARGS(ractl, max_page));
DECLARE_HOOK(android_vh_tune_mmap_readaround,
	TP_PROTO(unsigned int ra_pages, pgoff_t pgoff,
		pgoff_t *start, unsigned int *size, unsigned int *async_size),
	TP_ARGS(ra_pages, pgoff, start, size, async_size));
DECLARE_HOOK(android_vh_madvise_cold_pageout_skip,
	TP_PROTO(struct vm_area_struct *vma, struct folio *folio, bool pageout, bool *need_skip),
	TP_ARGS(vma, folio, pageout, need_skip));
struct mem_cgroup;
DECLARE_HOOK(android_vh_mem_cgroup_alloc,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_mem_cgroup_free,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_mem_cgroup_id_remove,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
struct cgroup_subsys_state;
DECLARE_HOOK(android_vh_mem_cgroup_css_online,
	TP_PROTO(struct cgroup_subsys_state *css, struct mem_cgroup *memcg),
	TP_ARGS(css, memcg));
DECLARE_HOOK(android_vh_mem_cgroup_css_offline,
	TP_PROTO(struct cgroup_subsys_state *css, struct mem_cgroup *memcg),
	TP_ARGS(css, memcg));
DECLARE_HOOK(android_vh_save_track_hash,
	TP_PROTO(bool alloc, struct track *p),
	TP_ARGS(alloc, p));
DECLARE_HOOK(android_vh_kmalloc_slab,
	TP_PROTO(unsigned int index, gfp_t flags, struct kmem_cache **s),
	TP_ARGS(index, flags, s));
DECLARE_HOOK(android_vh_adjust_kvmalloc_flags,
	TP_PROTO(unsigned int order, gfp_t *alloc_flags),
	TP_ARGS(order, alloc_flags));
DECLARE_HOOK(android_vh_alloc_pages_slowpath,
	TP_PROTO(gfp_t gfp_mask, unsigned int order, unsigned long delta),
	TP_ARGS(gfp_mask, order, delta));
DECLARE_HOOK(android_vh_alloc_pages_slowpath_start,
	TP_PROTO(u64 *stime),
	TP_ARGS(stime));
DECLARE_HOOK(android_vh_alloc_pages_slowpath_end,
	TP_PROTO(gfp_t *gfp_mask, unsigned int order, unsigned long alloc_start,
		u64 stime, unsigned long did_some_progress,
		unsigned long pages_reclaimed, int retry_loop_count),
	TP_ARGS(gfp_mask, order, alloc_start, stime, did_some_progress,
		pages_reclaimed, retry_loop_count));
DECLARE_HOOK(android_vh_dm_bufio_shrink_scan_bypass,
	TP_PROTO(unsigned long dm_bufio_current_allocated, bool *bypass),
	TP_ARGS(dm_bufio_current_allocated, bypass));
DECLARE_HOOK(android_vh_cleanup_old_buffers_bypass,
	TP_PROTO(unsigned long dm_bufio_current_allocated,
		unsigned long *max_age_hz,
=======
DECLARE_HOOK(android_vh_free_unref_page_bypass,
	TP_PROTO(struct page *page, int order, int migratetype, bool *bypass),
	TP_ARGS(page, order, migratetype, bypass));
DECLARE_HOOK(android_vh_kvmalloc_node_use_vmalloc,
	TP_PROTO(size_t size, gfp_t *kmalloc_flags, bool *use_vmalloc),
	TP_ARGS(size, kmalloc_flags, use_vmalloc));
DECLARE_HOOK(android_vh_should_alloc_pages_retry,
	TP_PROTO(gfp_t gfp_mask, int order, int *alloc_flags,
	int migratetype, struct zone *preferred_zone, struct page **page, bool *should_alloc_retry),
	TP_ARGS(gfp_mask, order, alloc_flags,
		migratetype, preferred_zone, page, should_alloc_retry));
DECLARE_HOOK(android_vh_alloc_pages_adjust_wmark,
	TP_PROTO(gfp_t gfp_mask, int order, int *alloc_flags),
	TP_ARGS(gfp_mask, order, alloc_flags));
DECLARE_HOOK(android_vh_alloc_pages_reset_wmark,
	TP_PROTO(gfp_t gfp_mask, int order, int *alloc_flags,
	unsigned long *did_some_progress, int *no_progress_loops,
	unsigned long direct_reclaim_retries),
	TP_ARGS(gfp_mask, order, alloc_flags, did_some_progress,
	no_progress_loops, direct_reclaim_retries));
DECLARE_HOOK(android_vh_unreserve_highatomic_bypass,
	TP_PROTO(bool force, struct zone *zone, bool *skip_unreserve_highatomic),
	TP_ARGS(force, zone, skip_unreserve_highatomic));
DECLARE_HOOK(android_vh_rmqueue_bulk_bypass,
	TP_PROTO(unsigned int order, struct per_cpu_pages *pcp, int migratetype,
		struct list_head *list),
	TP_ARGS(order, pcp, migratetype, list));
DECLARE_HOOK(android_vh_ra_tuning_max_page,
	TP_PROTO(struct readahead_control *ractl, unsigned long *max_page),
	TP_ARGS(ractl, max_page));
DECLARE_HOOK(android_vh_tune_mmap_readaround,
	TP_PROTO(unsigned int ra_pages, pgoff_t pgoff,
		pgoff_t *start, unsigned int *size, unsigned int *async_size),
	TP_ARGS(ra_pages, pgoff, start, size, async_size));
DECLARE_HOOK(android_vh_madvise_cold_pageout_skip,
	TP_PROTO(struct vm_area_struct *vma, struct folio *folio, bool pageout, bool *need_skip),
	TP_ARGS(vma, folio, pageout, need_skip));
struct mem_cgroup;
DECLARE_HOOK(android_vh_mem_cgroup_alloc,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_mem_cgroup_free,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
DECLARE_HOOK(android_vh_mem_cgroup_id_remove,
	TP_PROTO(struct mem_cgroup *memcg),
	TP_ARGS(memcg));
struct cgroup_subsys_state;
DECLARE_HOOK(android_vh_mem_cgroup_css_online,
	TP_PROTO(struct cgroup_subsys_state *css, struct mem_cgroup *memcg),
	TP_ARGS(css, memcg));
DECLARE_HOOK(android_vh_mem_cgroup_css_offline,
	TP_PROTO(struct cgroup_subsys_state *css, struct mem_cgroup *memcg),
	TP_ARGS(css, memcg));
DECLARE_HOOK(android_vh_save_track_hash,
	TP_PROTO(bool alloc, struct track *p),
	TP_ARGS(alloc, p));
DECLARE_HOOK(android_vh_should_fault_around,
	TP_PROTO(struct vm_fault *vmf, bool *should_around),
	TP_ARGS(vmf, should_around));
DECLARE_HOOK(android_vh_kmalloc_slab,
	TP_PROTO(unsigned int index, gfp_t flags, struct kmem_cache **s),
	TP_ARGS(index, flags, s));
DECLARE_HOOK(android_vh_adjust_kvmalloc_flags,
	TP_PROTO(unsigned int order, gfp_t *alloc_flags),
	TP_ARGS(order, alloc_flags));
DECLARE_HOOK(android_vh_alloc_pages_slowpath,
	TP_PROTO(gfp_t gfp_mask, unsigned int order, unsigned long delta),
	TP_ARGS(gfp_mask, order, delta));
DECLARE_HOOK(android_vh_alloc_pages_slowpath_start,
	TP_PROTO(u64 *stime),
	TP_ARGS(stime));
DECLARE_HOOK(android_vh_alloc_pages_slowpath_end,
	TP_PROTO(gfp_t *gfp_mask, unsigned int order, unsigned long alloc_start,
		u64 stime, unsigned long did_some_progress,
		unsigned long pages_reclaimed, int retry_loop_count),
	TP_ARGS(gfp_mask, order, alloc_start, stime, did_some_progress,
		pages_reclaimed, retry_loop_count));
DECLARE_HOOK(android_vh_dm_bufio_shrink_scan_bypass,
	TP_PROTO(unsigned long dm_bufio_current_allocated, bool *bypass),
	TP_ARGS(dm_bufio_current_allocated, bypass));
DECLARE_HOOK(android_vh_cleanup_old_buffers_bypass,
	TP_PROTO(unsigned long dm_bufio_current_allocated,
		unsigned long *max_age_hz,
>>>>>>> CHANGE (65ebb0 ANDROID: vendor_hooks: add hook to perform targeted memory m)
		bool *bypass),
	TP_ARGS(gfp_mask, alloc_flags, bypass));

DECLARE_HOOK(android_vh_slab_folio_alloced,
	TP_PROTO(unsigned int order, gfp_t flags),
	TP_ARGS(order, flags));
DECLARE_HOOK(android_vh_kmalloc_large_alloced,
	TP_PROTO(struct folio *folio, unsigned int order, gfp_t flags),
	TP_ARGS(folio, order, flags));
DECLARE_RESTRICTED_HOOK(android_rvh_ctl_dirty_rate,
	TP_PROTO(struct inode *inode),
	TP_ARGS(inode), 1);

DECLARE_HOOK(android_vh_alloc_pages_entry,
	TP_PROTO(gfp_t *gfp, unsigned int order, int preferred_nid,
		nodemask_t *nodemask),
	TP_ARGS(gfp, order, preferred_nid, nodemask));

DECLARE_HOOK(android_vh_free_unref_folios_to_pcp_bypass,
	TP_PROTO(struct folio_batch *folios, bool *bypass),
	TP_ARGS(folios, bypass));
DECLARE_HOOK(android_vh_cma_alloc_fail,
	TP_PROTO(char *name, unsigned long count, unsigned long req_count),
	TP_ARGS(name, count, req_count));
DECLARE_RESTRICTED_HOOK(android_rvh_vmalloc_node_bypass,
	TP_PROTO(unsigned long size, gfp_t gfp_mask, void **addr),
	TP_ARGS(size, gfp_mask, addr), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_vfree_bypass,
	TP_PROTO(const void *addr, bool *bypass),
	TP_ARGS(addr, bypass), 1);
#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
