/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mm

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_MM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MM_H

#include <linux/types.h>

#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct cma;
struct swap_slots_cache;
enum sgp_type;

DECLARE_RESTRICTED_HOOK(android_rvh_set_skip_swapcache_flags,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_gfp_zone_flags,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_readahead_gfp_mask,
			TP_PROTO(gfp_t *flags),
			TP_ARGS(flags), 1);
DECLARE_HOOK(android_vh_cma_alloc_start,
	TP_PROTO(s64 *ts),
	TP_ARGS(ts));
DECLARE_HOOK(android_vh_cma_alloc_finish,
	TP_PROTO(struct cma *cma, struct page *page, unsigned long count,
		 unsigned int align, gfp_t gfp_mask, s64 ts),
	TP_ARGS(cma, page, count, align, gfp_mask, ts));
DECLARE_HOOK(android_vh_rmqueue,
	TP_PROTO(struct zone *preferred_zone, struct zone *zone,
		unsigned int order, gfp_t gfp_flags,
		unsigned int alloc_flags, int migratetype),
	TP_ARGS(preferred_zone, zone, order,
		gfp_flags, alloc_flags, migratetype));
DECLARE_HOOK(android_vh_pagecache_get_page,
	TP_PROTO(struct address_space *mapping, pgoff_t index,
		int fgp_flags, gfp_t gfp_mask, struct page *page),
	TP_ARGS(mapping, index, fgp_flags, gfp_mask, page));
DECLARE_HOOK(android_vh_filemap_fault_get_page,
	TP_PROTO(struct vm_fault *vmf, struct page **page, bool *retry),
	TP_ARGS(vmf, page, retry));
DECLARE_HOOK(android_vh_filemap_fault_cache_page,
	TP_PROTO(struct vm_fault *vmf, struct page *page),
	TP_ARGS(vmf, page));
DECLARE_HOOK(android_vh_meminfo_proc_show,
	TP_PROTO(struct seq_file *m),
	TP_ARGS(m));
DECLARE_HOOK(android_vh_exit_mm,
	TP_PROTO(struct mm_struct *mm),
	TP_ARGS(mm));
DECLARE_HOOK(android_vh_get_from_fragment_pool,
	TP_PROTO(struct mm_struct *mm, struct vm_unmapped_area_info *info,
		unsigned long *addr),
	TP_ARGS(mm, info, addr));
DECLARE_HOOK(android_vh_exclude_reserved_zone,
	TP_PROTO(struct mm_struct *mm, struct vm_unmapped_area_info *info),
	TP_ARGS(mm, info));
DECLARE_HOOK(android_vh_include_reserved_zone,
	TP_PROTO(struct mm_struct *mm, struct vm_unmapped_area_info *info,
		unsigned long *addr),
	TP_ARGS(mm, info, addr));
DECLARE_HOOK(android_vh_show_mem,
	TP_PROTO(unsigned int filter, nodemask_t *nodemask),
	TP_ARGS(filter, nodemask));
struct slabinfo;
struct dirty_throttle_control;
DECLARE_HOOK(android_vh_mm_dirty_limits,
	TP_PROTO(struct dirty_throttle_control *const gdtc, bool strictlimit,
		unsigned long dirty, unsigned long bg_thresh,
		unsigned long nr_reclaimable, unsigned long pages_dirtied),
	TP_ARGS(gdtc, strictlimit, dirty, bg_thresh,
		nr_reclaimable, pages_dirtied));
DECLARE_HOOK(android_vh_oom_check_panic,
	TP_PROTO(struct oom_control *oc, int *ret),
	TP_ARGS(oc, ret));
DECLARE_HOOK(android_vh_save_vmalloc_stack,
	TP_PROTO(unsigned long flags, struct vm_struct *vm),
	TP_ARGS(flags, vm));
DECLARE_HOOK(android_vh_show_stack_hash,
	TP_PROTO(struct seq_file *m, struct vm_struct *v),
	TP_ARGS(m, v));
DECLARE_HOOK(android_vh_save_track_hash,
	TP_PROTO(bool alloc, unsigned long p),
	TP_ARGS(alloc, p));
struct mem_cgroup;
DECLARE_HOOK(android_vh_vmpressure,
	TP_PROTO(struct mem_cgroup *memcg, bool *bypass),
	TP_ARGS(memcg, bypass));
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
DECLARE_HOOK(android_vh_kmalloc_slab,
	TP_PROTO(unsigned int index, gfp_t flags, struct kmem_cache **s),
	TP_ARGS(index, flags, s));
DECLARE_HOOK(android_vh_mmap_region,
	TP_PROTO(struct vm_area_struct *vma, unsigned long addr),
	TP_ARGS(vma, addr));
DECLARE_HOOK(android_vh_try_to_unmap_one,
	TP_PROTO(struct vm_area_struct *vma, struct page *page, unsigned long addr, bool ret),
	TP_ARGS(vma, page, addr, ret));
DECLARE_HOOK(android_vh_drain_all_pages_bypass,
	TP_PROTO(gfp_t gfp_mask, unsigned int order, unsigned long alloc_flags,
		int migratetype, unsigned long did_some_progress,
		bool *bypass),
	TP_ARGS(gfp_mask, order, alloc_flags, migratetype, did_some_progress, bypass));
DECLARE_HOOK(android_vh_cma_drain_all_pages_bypass,
	TP_PROTO(unsigned int migratetype, bool *bypass),
	TP_ARGS(migratetype, bypass));
DECLARE_HOOK(android_vh_pcplist_add_cma_pages_bypass,
	TP_PROTO(int migratetype, bool *bypass),
	TP_ARGS(migratetype, bypass));
struct device;
DECLARE_HOOK(android_vh_subpage_dma_contig_alloc,
	TP_PROTO(bool *allow_subpage_alloc, struct device *dev, size_t *size),
	TP_ARGS(allow_subpage_alloc, dev, size));
struct readahead_control;
DECLARE_HOOK(android_vh_ra_tuning_max_page,
	TP_PROTO(struct readahead_control *ractl, unsigned long *max_page),
	TP_ARGS(ractl, max_page));
DECLARE_RESTRICTED_HOOK(android_rvh_do_access_page,
	TP_PROTO(struct vm_fault *vmf),
	TP_ARGS(vmf), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_vma_flag_process,
	TP_PROTO(struct vm_fault *vmf, struct page *page, int method),
	TP_ARGS(vmf, page, method), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_process_page_states,
	TP_PROTO(struct page *page1, struct page *page2, int method),
	TP_ARGS(page1, page2, method), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_page_referenced_one,
	TP_PROTO(struct vm_area_struct *vma, struct page *page),
	TP_ARGS(vma, page), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_update_swap_ref_cnt,
	TP_PROTO(struct swap_info_struct *sis, int access),
	TP_ARGS(sis, access), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_count_swpout_vm_event,
	TP_PROTO(struct swap_info_struct *sis, struct page *page, int *ret, int val),
	TP_ARGS(sis, page, ret, val), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_shmem_unuse_swap_entries,
	TP_PROTO(struct inode *inode, void *pvec_point, pgoff_t *indices, unsigned int type,
		int (*shmem_swapin_page)(struct inode *inode, pgoff_t index,
			struct page **pagep, enum sgp_type sgp, gfp_t gfp,
			struct vm_area_struct *vma, vm_fault_t *fault_type),
		int *ret, bool *skip),
	TP_ARGS(inode, pvec_point, indices, type, shmem_swapin_page, ret, skip), 1);
DECLARE_HOOK(android_vh_swap_slot_cache_active,
	TP_PROTO(bool swap_slot_cache_active),
	TP_ARGS(swap_slot_cache_active), 1);
DECLARE_HOOK(android_vh_drain_slots_cache_cpu,
	TP_PROTO(struct swap_slots_cache *cache, unsigned int type,
		bool free_slots, bool *skip),
	TP_ARGS(cache, type, free_slots, skip), 1);
DECLARE_HOOK(android_vh_alloc_swap_slot_cache,
	TP_PROTO(struct swap_slots_cache *cache,  swp_entry_t **slots,
		swp_entry_t **slots_ret, int *ret, bool *skip),
	TP_ARGS(cache, slots, slots_ret, ret, skip), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_free_swap_slot,
	TP_PROTO(swp_entry_t entry, struct swap_info_struct *sis, bool *skip),
	TP_ARGS(entry, sis, skip), 1);
DECLARE_HOOK(android_vh_get_swap_slot_info,
	TP_PROTO(struct page *page, struct swap_slots_cache *cache,
		bool (*check_cache_active)(void), swp_entry_t *entry, bool *skip),
	TP_ARGS(page, cache, check_cache_active, entry, skip), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_get_swap_page,
	TP_PROTO(struct page *page, u64 hotness, swp_entry_t *entry, bool *skip),
	TP_ARGS(page, hotness, entry, skip), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_madvise_case_memfusion,
	TP_PROTO(int *ret, int behavior),
	TP_ARGS(ret, behavior), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_madvise_vma,
	TP_PROTO(int *ret, struct vm_area_struct *vma, struct vm_area_struct **prev,
		unsigned long start, unsigned long end, int behavior),
	TP_ARGS(ret, vma, prev, start, end,  behavior), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_add_to_swap,
	TP_PROTO(int *error),
	TP_ARGS(error), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_nr_swap_pages,
	TP_PROTO(struct swap_info_struct *si, unsigned int nr_entries,
		atomic_long_t *nr_swap_pages, int *ret, int type),
	TP_ARGS(si, nr_entries, nr_swap_pages, ret, type), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_set_page_hot,
	TP_PROTO(struct swap_info_struct *si, struct page *page),
	TP_ARGS(si, page), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_add_nr_total_swap_pages,
	TP_PROTO(struct swap_info_struct *p, atomic_long_t *nr_swap_pages,
		long *total_swap_pages, int *ret),
	TP_ARGS(p, nr_swap_pages, total_swap_pages, ret), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_init_swap_info_struct,
	TP_PROTO(struct swap_info_struct *p, struct plist_head *swap_avail_heads),
	TP_ARGS(p, swap_avail_heads), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_si_swapinfo,
	TP_PROTO(struct swap_info_struct *si, unsigned long *nr_to_be_unused, int *ret),
	TP_ARGS(si, nr_to_be_unused, ret), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_get_functions,
	TP_PROTO(unsigned int nr_swapfiles, int (*function1)(struct swap_info_struct *si,
			unsigned char usage, int nr, swp_entry_t slots[]),
		int (*function2)(struct swap_info_struct *si, swp_entry_t *slot)),
	TP_ARGS(nr_swapfiles, function1, function2), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_alloc_si,
	TP_PROTO(struct swap_info_struct **p, int *ret),
	TP_ARGS(p, ret), 1);
/* macro versions of hooks are no longer required */

#endif /* _TRACE_HOOK_MM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
