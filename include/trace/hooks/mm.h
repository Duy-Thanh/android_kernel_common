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

DECLARE_HOOK(android_vh_rmqueue_smallest_bypass,
	TP_PROTO(struct page **page, struct zone *zone, int order, int migratetype),
	TP_ARGS(page, zone, order, migratetype));
DECLARE_HOOK(android_vh_free_one_page_bypass,
	TP_PROTO(struct page *page, struct zone *zone, int order, int migratetype,
		int fpi_flags, bool *bypass),
	TP_ARGS(page, zone, order, migratetype, fpi_flags, bypass));

struct page_vma_mapped_walk;
DECLARE_HOOK(android_vh_slab_alloc_node,
	TP_PROTO(void *object, unsigned long addr, struct kmem_cache *s),
	TP_ARGS(object, addr, s));
DECLARE_HOOK(android_vh_slab_free,
	TP_PROTO(unsigned long addr, struct kmem_cache *s),
	TP_ARGS(addr, s));
DECLARE_HOOK(android_vh_process_madvise_begin,
	TP_PROTO(struct task_struct *task, int behavior),
	TP_ARGS(task, behavior));
DECLARE_HOOK(android_vh_process_madvise_iter,
	TP_PROTO(struct task_struct *task, int behavior, ssize_t *ret),
	TP_ARGS(task, behavior, ret));
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
<<<<<<< HEAD   (0fc7ac ANDROID: microdroid: disable unneeded networking options)
||||||| BASE
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
DECLARE_HOOK(android_vh_mmap_region,
	TP_PROTO(struct vm_area_struct *vma, unsigned long addr),
	TP_ARGS(vma, addr));
DECLARE_HOOK(android_vh_try_to_unmap_one,
	TP_PROTO(struct folio *folio, struct vm_area_struct *vma,
		unsigned long addr, void *arg, bool ret),
	TP_ARGS(folio, vma, addr, arg, ret));
DECLARE_HOOK(android_vh_mm_direct_reclaim_enter,
	TP_PROTO(unsigned int order),
	TP_ARGS(order));
DECLARE_HOOK(android_vh_mm_direct_reclaim_exit,
	TP_PROTO(unsigned long did_some_progress, int retry_times),
	TP_ARGS(did_some_progress, retry_times));
struct oom_control;
DECLARE_HOOK(android_vh_mm_may_oom_exit,
	TP_PROTO(struct oom_control *oc, unsigned long did_some_progress),
	TP_ARGS(oc, did_some_progress));
DECLARE_HOOK(android_vh_do_anonymous_page,
	TP_PROTO(struct vm_area_struct *vma, struct folio *folio),
	TP_ARGS(vma, folio));
DECLARE_HOOK(android_vh_do_swap_page,
	TP_PROTO(struct folio *folio, pte_t *pte, struct vm_fault *vmf,
		swp_entry_t entry),
	TP_ARGS(folio, pte, vmf, entry));
DECLARE_HOOK(android_vh_do_wp_page,
	TP_PROTO(struct folio *folio),
	TP_ARGS(folio));
DECLARE_HOOK(android_vh_uprobes_replace_page,
	TP_PROTO(struct folio *new_folio, struct folio *old_folio),
	TP_ARGS(new_folio, old_folio));
DECLARE_HOOK(android_vh_shmem_swapin_folio,
	TP_PROTO(struct folio *folio),
	TP_ARGS(folio));
DECLARE_HOOK(android_vh_get_page_wmark,
	TP_PROTO(unsigned int alloc_flags, unsigned long *page_wmark),
	TP_ARGS(alloc_flags, page_wmark));
DECLARE_HOOK(android_vh_page_add_new_anon_rmap,
	TP_PROTO(struct page *page, struct vm_area_struct *vma,
		unsigned long address),
	TP_ARGS(page, vma, address));
DECLARE_HOOK(android_vh_customize_alloc_gfp,
	TP_PROTO(gfp_t *alloc_gfp, unsigned int order),
	TP_ARGS(alloc_gfp, order));
=======
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
DECLARE_HOOK(android_vh_mmap_region,
	TP_PROTO(struct vm_area_struct *vma, unsigned long addr),
	TP_ARGS(vma, addr));
DECLARE_HOOK(android_vh_try_to_unmap_one,
	TP_PROTO(struct folio *folio, struct vm_area_struct *vma,
		unsigned long addr, void *arg, bool ret),
	TP_ARGS(folio, vma, addr, arg, ret));
DECLARE_HOOK(android_vh_mm_direct_reclaim_enter,
	TP_PROTO(unsigned int order),
	TP_ARGS(order));
DECLARE_HOOK(android_vh_mm_direct_reclaim_exit,
	TP_PROTO(unsigned long did_some_progress, int retry_times),
	TP_ARGS(did_some_progress, retry_times));
struct oom_control;
DECLARE_HOOK(android_vh_mm_may_oom_exit,
	TP_PROTO(struct oom_control *oc, unsigned long did_some_progress),
	TP_ARGS(oc, did_some_progress));
DECLARE_HOOK(android_vh_do_anonymous_page,
	TP_PROTO(struct vm_area_struct *vma, struct folio *folio),
	TP_ARGS(vma, folio));
DECLARE_HOOK(android_vh_do_swap_page,
	TP_PROTO(struct folio *folio, pte_t *pte, struct vm_fault *vmf,
		swp_entry_t entry),
	TP_ARGS(folio, pte, vmf, entry));
DECLARE_HOOK(android_vh_do_wp_page,
	TP_PROTO(struct folio *folio),
	TP_ARGS(folio));
DECLARE_HOOK(android_vh_uprobes_replace_page,
	TP_PROTO(struct folio *new_folio, struct folio *old_folio),
	TP_ARGS(new_folio, old_folio));
DECLARE_HOOK(android_vh_shmem_swapin_folio,
	TP_PROTO(struct folio *folio),
	TP_ARGS(folio));
DECLARE_HOOK(android_vh_get_page_wmark,
	TP_PROTO(unsigned int alloc_flags, unsigned long *page_wmark),
	TP_ARGS(alloc_flags, page_wmark));
DECLARE_HOOK(android_vh_page_add_new_anon_rmap,
	TP_PROTO(struct page *page, struct vm_area_struct *vma,
		unsigned long address),
	TP_ARGS(page, vma, address));
DECLARE_HOOK(android_vh_customize_alloc_gfp,
	TP_PROTO(gfp_t *alloc_gfp, unsigned int order),
	TP_ARGS(alloc_gfp, order));
DECLARE_HOOK(android_vh_madvise_swapin_walk_pmd_entry,
	TP_PROTO(swp_entry_t entry),
	TP_ARGS(entry));
DECLARE_HOOK(android_vh_process_madvise,
	TP_PROTO(int behavior, ssize_t *ret, void *priv),
	TP_ARGS(behavior, ret, priv));
>>>>>>> CHANGE (7adcca ANDROID: mm: add vendor hooks in madvise for swap entry)

DECLARE_HOOK(android_vh_count_workingset_refault,
	TP_PROTO(struct folio *folio),
	TP_ARGS(folio));
DECLARE_HOOK(android_vh_calc_alloc_flags,
	TP_PROTO(gfp_t gfp_mask, unsigned int *alloc_flags,
		bool *bypass),
	TP_ARGS(gfp_mask, alloc_flags, bypass));

DECLARE_HOOK(android_vh_should_fault_around,
	TP_PROTO(struct vm_fault *vmf, bool *should_around),
	TP_ARGS(vmf, should_around));
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
DECLARE_HOOK(android_vh_cma_alloc_retry,
	TP_PROTO(char *name, int *retry),
	TP_ARGS(name, retry));
#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
