/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <trace/hooks/vendor_hooks.h>

<<<<<<< HEAD   (c8ad90 ANDROID: abi_gki_aarch64_qcom: ufshcd_mcq_poll_cqe_lock)
struct shmem_inode_info;
struct folio;
=======
#ifdef __GENKSYMS__
struct slabinfo;
struct cgroup_subsys_state;
struct device;
struct mem_cgroup;
struct readahead_control;
#else
/* struct slabinfo */
#include <../mm/slab.h>
/* struct cgroup_subsys_state */
#include <linux/cgroup-defs.h>
/* struct device */
#include <linux/device.h>
/* struct mem_cgroup */
#include <linux/memcontrol.h>
/* struct readahead_control */
#include <linux/pagemap.h>
#endif /* __GENKSYMS__ */
struct cma;
struct swap_slots_cache;
struct page_vma_mapped_walk;
>>>>>>> CHANGE (f50f24 ANDROID: vendor_hooks: Add hooks for lookaround)

DECLARE_RESTRICTED_HOOK(android_rvh_shmem_get_folio,
			TP_PROTO(struct shmem_inode_info *info, struct folio **folio),
			TP_ARGS(info, folio), 2);

DECLARE_RESTRICTED_HOOK(android_rvh_set_gfp_zone_flags,
			TP_PROTO(unsigned int *flags),	/* gfp_t *flags */
			TP_ARGS(flags), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_set_readahead_gfp_mask,
			TP_PROTO(unsigned int *flags),	/* gfp_t *flags */
			TP_ARGS(flags), 1);
DECLARE_HOOK(android_vh_dm_bufio_shrink_scan_bypass,
	TP_PROTO(unsigned long dm_bufio_current_allocated, bool *bypass),
	TP_ARGS(dm_bufio_current_allocated, bypass));
DECLARE_HOOK(android_vh_cleanup_old_buffers_bypass,
	TP_PROTO(unsigned long dm_bufio_current_allocated,
		unsigned long *max_age_hz,
		bool *bypass),
	TP_ARGS(dm_bufio_current_allocated, max_age_hz, bypass));
DECLARE_HOOK(android_vh_mmap_region,
	TP_PROTO(struct vm_area_struct *vma, unsigned long addr),
	TP_ARGS(vma, addr));
DECLARE_HOOK(android_vh_try_to_unmap_one,
	TP_PROTO(struct folio *folio, struct vm_area_struct *vma,
		unsigned long addr, void *arg, bool ret),
	TP_ARGS(folio, vma, addr, arg, ret));
DECLARE_HOOK(android_vh_get_page_wmark,
	TP_PROTO(unsigned int alloc_flags, unsigned long *page_wmark),
	TP_ARGS(alloc_flags, page_wmark));
DECLARE_HOOK(android_vh_page_add_new_anon_rmap,
	TP_PROTO(struct page *page, struct vm_area_struct *vma,
		unsigned long address),
	TP_ARGS(page, vma, address));
DECLARE_HOOK(android_vh_meminfo_proc_show,
	TP_PROTO(struct seq_file *m),
	TP_ARGS(m));
DECLARE_HOOK(android_vh_exit_mm,
	TP_PROTO(struct mm_struct *mm),
	TP_ARGS(mm));
DECLARE_HOOK(android_vh_show_mem,
	TP_PROTO(unsigned int filter, nodemask_t *nodemask),
	TP_ARGS(filter, nodemask));
DECLARE_HOOK(android_vh_alloc_pages_slowpath,
	TP_PROTO(gfp_t gfp_mask, unsigned int order, unsigned long delta),
	TP_ARGS(gfp_mask, order, delta));
DECLARE_HOOK(android_vh_print_slabinfo_header,
	TP_PROTO(struct seq_file *m),
	TP_ARGS(m));
struct slabinfo;
DECLARE_HOOK(android_vh_cache_show,
	TP_PROTO(struct seq_file *m, struct slabinfo *sinfo, struct kmem_cache *s),
	TP_ARGS(m, sinfo, s));
DECLARE_HOOK(android_vh_alloc_pages_reclaim_bypass,
    TP_PROTO(gfp_t gfp_mask, int order, int alloc_flags,
	int migratetype, struct page **page),
	TP_ARGS(gfp_mask, order, alloc_flags, migratetype, page));
DECLARE_HOOK(android_vh_alloc_pages_failure_bypass,
	TP_PROTO(gfp_t gfp_mask, int order, int alloc_flags,
	int migratetype, struct page **page),
	TP_ARGS(gfp_mask, order, alloc_flags, migratetype, page));
DECLARE_HOOK(android_vh_madvise_pageout_swap_entry,
	TP_PROTO(swp_entry_t entry, int swapcount),
	TP_ARGS(entry, swapcount));
DECLARE_HOOK(android_vh_madvise_swapin_walk_pmd_entry,
	TP_PROTO(swp_entry_t entry),
	TP_ARGS(entry));
DECLARE_HOOK(android_vh_process_madvise_end,
	TP_PROTO(int behavior, ssize_t *ret),
	TP_ARGS(behavior, ret));
DECLARE_HOOK(android_vh_smaps_pte_entry,
	TP_PROTO(swp_entry_t entry, unsigned long *writeback,
		unsigned long *same, unsigned long *huge),
	TP_ARGS(entry, writeback, same, huge));
DECLARE_HOOK(android_vh_show_smap,
	TP_PROTO(struct seq_file *m, unsigned long writeback,
		unsigned long same, unsigned long huge),
	TP_ARGS(m, writeback, same, huge));
DECLARE_HOOK(android_vh_meminfo_cache_adjust,
	TP_PROTO(unsigned long *cached),
	TP_ARGS(cached));
DECLARE_HOOK(android_vh_si_mem_available_adjust,
	TP_PROTO(unsigned long *available),
	TP_ARGS(available));
DECLARE_HOOK(android_vh_si_meminfo_adjust,
	TP_PROTO(unsigned long *totalram, unsigned long *freeram),
	TP_ARGS(totalram, freeram));
DECLARE_RESTRICTED_HOOK(android_rvh_ctl_dirty_rate,
	TP_PROTO(void *unused),
	TP_ARGS(unused), 1);
DECLARE_HOOK(android_vh_madvise_cold_pageout_skip,
	TP_PROTO(struct vm_area_struct *vma, struct page *page, bool pageout, bool *need_skip),
	TP_ARGS(vma, page, pageout, need_skip));

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
<<<<<<< HEAD   (c8ad90 ANDROID: abi_gki_aarch64_qcom: ufshcd_mcq_poll_cqe_lock)
DECLARE_HOOK(android_vh_rmqueue_smallest_bypass,
	TP_PROTO(struct page **page, struct zone *zone, int order, int migratetype),
	TP_ARGS(page, zone, order, migratetype));
DECLARE_HOOK(android_vh_free_one_page_bypass,
	TP_PROTO(struct page *page, struct zone *zone, int order, int migratetype,
		int fpi_flags, bool *bypass),
	TP_ARGS(page, zone, order, migratetype, fpi_flags, bypass));
=======
DECLARE_HOOK(android_vh_kmalloc_slab,
	TP_PROTO(unsigned int index, gfp_t flags, struct kmem_cache **s),
	TP_ARGS(index, flags, s));
DECLARE_HOOK(android_vh_mmap_region,
	TP_PROTO(struct vm_area_struct *vma, unsigned long addr),
	TP_ARGS(vma, addr));
DECLARE_HOOK(android_vh_try_to_unmap_one,
	TP_PROTO(struct vm_area_struct *vma, struct page *page, unsigned long addr, bool ret),
	TP_ARGS(vma, page, addr, ret));
DECLARE_HOOK(android_vh_do_page_trylock,
	TP_PROTO(struct page *page, struct rw_semaphore *sem,
		bool *got_lock, bool *success),
	TP_ARGS(page, sem, got_lock, success));
DECLARE_HOOK(android_vh_drain_all_pages_bypass,
	TP_PROTO(gfp_t gfp_mask, unsigned int order, unsigned long alloc_flags,
		int migratetype, unsigned long did_some_progress,
		bool *bypass),
	TP_ARGS(gfp_mask, order, alloc_flags, migratetype, did_some_progress, bypass));
DECLARE_HOOK(android_vh_update_page_mapcount,
	TP_PROTO(struct page *page, bool inc_size, bool compound,
			bool *first_mapping, bool *success),
	TP_ARGS(page, inc_size, compound, first_mapping, success));
DECLARE_HOOK(android_vh_add_page_to_lrulist,
	TP_PROTO(struct page *page, bool compound, enum lru_list lru),
	TP_ARGS(page, compound, lru));
DECLARE_HOOK(android_vh_del_page_from_lrulist,
	TP_PROTO(struct page *page, bool compound, enum lru_list lru),
	TP_ARGS(page, compound, lru));
DECLARE_HOOK(android_vh_show_mapcount_pages,
	TP_PROTO(void *unused),
	TP_ARGS(unused));
DECLARE_HOOK(android_vh_do_traversal_lruvec,
	TP_PROTO(struct lruvec *lruvec),
	TP_ARGS(lruvec));
DECLARE_HOOK(android_vh_page_should_be_protected,
	TP_PROTO(struct page *page, bool *should_protect),
	TP_ARGS(page, should_protect));
DECLARE_HOOK(android_vh_mark_page_accessed,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_cma_drain_all_pages_bypass,
	TP_PROTO(unsigned int migratetype, bool *bypass),
	TP_ARGS(migratetype, bypass));
DECLARE_HOOK(android_vh_pcplist_add_cma_pages_bypass,
	TP_PROTO(int migratetype, bool *bypass),
	TP_ARGS(migratetype, bypass));
DECLARE_HOOK(android_vh_subpage_dma_contig_alloc,
	TP_PROTO(bool *allow_subpage_alloc, struct device *dev, size_t *size),
	TP_ARGS(allow_subpage_alloc, dev, size));
DECLARE_HOOK(android_vh_handle_pte_fault_end,
	TP_PROTO(struct vm_fault *vmf, unsigned long highest_memmap_pfn),
	TP_ARGS(vmf, highest_memmap_pfn));
DECLARE_HOOK(android_vh_cow_user_page,
	TP_PROTO(struct vm_fault *vmf, struct page *page),
	TP_ARGS(vmf, page));
DECLARE_HOOK(android_vh_swapin_add_anon_rmap,
	TP_PROTO(struct vm_fault *vmf, struct page *page),
	TP_ARGS(vmf, page));
DECLARE_HOOK(android_vh_waiting_for_page_migration,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_migrate_page_states,
	TP_PROTO(struct page *page, struct page *newpage),
	TP_ARGS(page, newpage));
DECLARE_HOOK(android_vh_page_referenced_one_end,
	TP_PROTO(struct vm_area_struct *vma, struct page *page, int referenced),
	TP_ARGS(vma, page, referenced));
DECLARE_HOOK(android_vh_count_pswpin,
	TP_PROTO(struct swap_info_struct *sis),
	TP_ARGS(sis));
DECLARE_HOOK(android_vh_count_pswpout,
	TP_PROTO(struct swap_info_struct *sis),
	TP_ARGS(sis));
DECLARE_HOOK(android_vh_count_swpout_vm_event,
	TP_PROTO(struct swap_info_struct *sis, struct page *page, bool *skip),
	TP_ARGS(sis, page, skip));
DECLARE_HOOK(android_vh_swap_slot_cache_active,
	TP_PROTO(bool swap_slot_cache_active),
	TP_ARGS(swap_slot_cache_active));
DECLARE_HOOK(android_vh_drain_slots_cache_cpu,
	TP_PROTO(struct swap_slots_cache *cache, unsigned int type,
		bool free_slots, bool *skip),
	TP_ARGS(cache, type, free_slots, skip));
DECLARE_HOOK(android_vh_alloc_swap_slot_cache,
	TP_PROTO(struct swap_slots_cache *cache, int *ret, bool *skip),
	TP_ARGS(cache, ret, skip));
DECLARE_HOOK(android_vh_free_swap_slot,
	TP_PROTO(swp_entry_t entry, struct swap_slots_cache *cache, bool *skip),
	TP_ARGS(entry, cache, skip));
DECLARE_HOOK(android_vh_get_swap_page,
	TP_PROTO(struct page *page, swp_entry_t *entry,
		struct swap_slots_cache *cache, bool *found),
	TP_ARGS(page, entry, cache, found));
DECLARE_HOOK(android_vh_page_isolated_for_reclaim,
	TP_PROTO(struct mm_struct *mm, struct page *page),
	TP_ARGS(mm, page));
DECLARE_HOOK(android_vh_account_swap_pages,
	TP_PROTO(struct swap_info_struct *si, bool *skip),
	TP_ARGS(si, skip));
DECLARE_HOOK(android_vh_unuse_swap_page,
	TP_PROTO(struct swap_info_struct *si, struct page *page),
	TP_ARGS(si, page));
DECLARE_HOOK(android_vh_init_swap_info_struct,
	TP_PROTO(struct swap_info_struct *p, struct plist_head *swap_avail_heads),
	TP_ARGS(p, swap_avail_heads));
DECLARE_HOOK(android_vh_si_swapinfo,
	TP_PROTO(struct swap_info_struct *si, bool *skip),
	TP_ARGS(si, skip));
DECLARE_HOOK(android_vh_alloc_si,
	TP_PROTO(struct swap_info_struct **p, bool *skip),
	TP_ARGS(p, skip));
DECLARE_HOOK(android_vh_free_pages,
	TP_PROTO(struct page *page, unsigned int order),
	TP_ARGS(page, order));
DECLARE_HOOK(android_vh_set_shmem_page_flag,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_remove_vmalloc_stack,
	TP_PROTO(struct vm_struct *vm),
	TP_ARGS(vm));
DECLARE_HOOK(android_vh_alloc_pages_reclaim_bypass,
	TP_PROTO(gfp_t gfp_mask, int order, int alloc_flags,
	int migratetype, struct page **page),
	TP_ARGS(gfp_mask, order, alloc_flags, migratetype, page));
DECLARE_HOOK(android_vh_alloc_pages_failure_bypass,
	TP_PROTO(gfp_t gfp_mask, int order, int alloc_flags,
	int migratetype, struct page **page),
	TP_ARGS(gfp_mask, order, alloc_flags, migratetype, page));
DECLARE_HOOK(android_vh_test_clear_look_around_ref,
	TP_PROTO(struct page *page),
	TP_ARGS(page));
DECLARE_HOOK(android_vh_look_around_migrate_page,
	TP_PROTO(struct page *old_page, struct page *new_page),
	TP_ARGS(old_page, new_page));
DECLARE_HOOK(android_vh_look_around,
	TP_PROTO(struct page_vma_mapped_walk *pvmw, struct page *page,
		struct vm_area_struct *vma, int *referenced),
	TP_ARGS(pvmw, page, vma, referenced));
/* macro versions of hooks are no longer required */
>>>>>>> CHANGE (f50f24 ANDROID: vendor_hooks: Add hooks for lookaround)

#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
