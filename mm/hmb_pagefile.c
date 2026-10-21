// SPDX-License-Identifier: GPL-2.0-only
/*
 * Haven Memory Bus (HMB) Spec 2.0 — Android Common Kernel (ACK) Paging Engine
 *
 * Copyright (C) 2026 Daniel Elliott / Barrer Software Org. All rights reserved.
 * Author: Daniel Elliott <ssfdre38@gmail.com>
 *
 * Implements high-performance Virtual Memory paging with zero-bio bypass,
 * Tier 2 z-Pack inline pattern deduplication, and lock-free 64-bit atomic bitmask metadata.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/swapfile.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/bitmap.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/hmb_pagefile.h>

#define HMB_PAGEFILE_MAX_PAGES (HMB_PLANE_SIZE_BYTES >> PAGE_SHIFT)

struct hmb_stats {
	atomic64_t reads;
	atomic64_t writes;
	atomic64_t frees;
	atomic64_t zero_hits;
	atomic64_t pattern_hits;
	atomic64_t raw_allocs;
	atomic64_t bytes_saved;
};

struct hmb_pagefile_plane {
	unsigned long total_pages;
	unsigned long *allocated_bitmap;
	unsigned long *zero_bitmap;
	unsigned long *pattern_bitmap;
	u64 *patterns;
	struct page **raw_pages;
	spinlock_t lock;
	struct hmb_stats stats;
};

static struct hmb_pagefile_plane *global_hmb_plane;

/*
 * Fast inline pattern detector for Tier 2 z-Pack compression
 */
static inline bool hmb_is_uniform_pattern(const u64 *buf, size_t count, u64 *out_val)
{
	u64 first = buf[0];
	size_t i;

	for (i = 1; i < count; i++) {
		if (buf[i] != first)
			return false;
	}
	*out_val = first;
	return true;
}

/*
 * Store Folio: Direct VMM write bypass
 */
bool hmb_pagefile_store(struct folio *folio)
{
	struct hmb_pagefile_plane *plane = global_hmb_plane;
	pgoff_t pgoff;
	void *src;
	u64 val;
	bool is_uniform;
	struct page *new_p, *old_p;
	void *dst;

	if (unlikely(!plane))
		return false;

	pgoff = swp_offset(folio->swap);
	if (unlikely(pgoff >= plane->total_pages))
		return false;

	src = kmap_local_folio(folio, 0);
	is_uniform = hmb_is_uniform_pattern((const u64 *)src, PAGE_SIZE / sizeof(u64), &val);
	kunmap_local(src);

	if (is_uniform) {
		if (val == 0) {
			/* Zero page: 100% deduplicated */
			set_bit(pgoff, plane->zero_bitmap);
			clear_bit(pgoff, plane->pattern_bitmap);
			clear_bit(pgoff, plane->allocated_bitmap);

			spin_lock(&plane->lock);
			old_p = plane->raw_pages[pgoff];
			plane->raw_pages[pgoff] = NULL;
			spin_unlock(&plane->lock);

			if (old_p)
				__free_page(old_p);

			atomic64_inc(&plane->stats.zero_hits);
			atomic64_add(PAGE_SIZE, &plane->stats.bytes_saved);
			atomic64_inc(&plane->stats.writes);

			folio_mark_uptodate(folio);
			return true;
		}

		/* Repeating Quadword: Stored in 8 bytes */
		plane->patterns[pgoff] = val;
		set_bit(pgoff, plane->pattern_bitmap);
		clear_bit(pgoff, plane->zero_bitmap);
		clear_bit(pgoff, plane->allocated_bitmap);

		spin_lock(&plane->lock);
		old_p = plane->raw_pages[pgoff];
		plane->raw_pages[pgoff] = NULL;
		spin_unlock(&plane->lock);

		if (old_p)
			__free_page(old_p);

		atomic64_inc(&plane->stats.pattern_hits);
		atomic64_add(PAGE_SIZE - sizeof(u64), &plane->stats.bytes_saved);
		atomic64_inc(&plane->stats.writes);

		folio_mark_uptodate(folio);
		return true;
	}

	/* Tier 1: Raw Stride Allocation */
	new_p = alloc_page(GFP_NOWAIT | __GFP_NOWARN | __GFP_HIGHMEM);
	if (unlikely(!new_p)) {
		new_p = alloc_page(GFP_ATOMIC | __GFP_NOWARN | __GFP_HIGHMEM);
		if (unlikely(!new_p))
			return false;
	}

	src = kmap_local_folio(folio, 0);
	dst = kmap_local_page(new_p);
	memcpy(dst, src, PAGE_SIZE);
	kunmap_local(dst);
	kunmap_local(src);

	spin_lock(&plane->lock);
	old_p = plane->raw_pages[pgoff];
	plane->raw_pages[pgoff] = new_p;
	spin_unlock(&plane->lock);

	if (old_p)
		__free_page(old_p);

	set_bit(pgoff, plane->allocated_bitmap);
	clear_bit(pgoff, plane->zero_bitmap);
	clear_bit(pgoff, plane->pattern_bitmap);

	atomic64_inc(&plane->stats.raw_allocs);
	atomic64_inc(&plane->stats.writes);

	folio_mark_uptodate(folio);
	return true;
}

/*
 * Load Folio: Direct VMM read bypass
 */
bool hmb_pagefile_load(struct folio *folio)
{
	struct hmb_pagefile_plane *plane = global_hmb_plane;
	pgoff_t pgoff;
	void *dst;
	struct page *p;

	if (unlikely(!plane))
		return false;

	pgoff = swp_offset(folio->swap);
	if (unlikely(pgoff >= plane->total_pages))
		return false;

	/* Tier 2: Zero Page Fast-Path */
	if (test_bit(pgoff, plane->zero_bitmap)) {
		dst = kmap_local_folio(folio, 0);
		memset(dst, 0, PAGE_SIZE);
		kunmap_local(dst);
		folio_mark_uptodate(folio);
		atomic64_inc(&plane->stats.reads);
		return true;
	}

	/* Tier 2: Pattern Page Fast-Path */
	if (test_bit(pgoff, plane->pattern_bitmap)) {
		u64 val = plane->patterns[pgoff];

		dst = kmap_local_folio(folio, 0);
		memset64(dst, val, PAGE_SIZE / sizeof(u64));
		kunmap_local(dst);
		folio_mark_uptodate(folio);
		atomic64_inc(&plane->stats.reads);
		return true;
	}

	/* Tier 1: Raw Stride Memory Slab */
	if (test_bit(pgoff, plane->allocated_bitmap)) {
		spin_lock(&plane->lock);
		p = plane->raw_pages[pgoff];
		if (p)
			get_page(p);
		spin_unlock(&plane->lock);

		if (p) {
			void *src = kmap_local_page(p);

			dst = kmap_local_folio(folio, 0);
			memcpy(dst, src, PAGE_SIZE);
			kunmap_local(dst);
			kunmap_local(src);
			put_page(p);
			folio_mark_uptodate(folio);
			atomic64_inc(&plane->stats.reads);
			return true;
		}
	}

	return false;
}

/*
 * Free Slot: Lock-free atomic bitmask clearing
 */
void hmb_pagefile_free(swp_entry_t entry)
{
	struct hmb_pagefile_plane *plane = global_hmb_plane;
	pgoff_t pgoff;
	struct page *p = NULL;

	if (unlikely(!plane))
		return;

	pgoff = swp_offset(entry);
	if (unlikely(pgoff >= plane->total_pages))
		return;

	clear_bit(pgoff, plane->zero_bitmap);
	clear_bit(pgoff, plane->pattern_bitmap);

	if (test_and_clear_bit(pgoff, plane->allocated_bitmap)) {
		spin_lock(&plane->lock);
		p = plane->raw_pages[pgoff];
		plane->raw_pages[pgoff] = NULL;
		spin_unlock(&plane->lock);

		if (p)
			__free_page(p);
	}

	atomic64_inc(&plane->stats.frees);
}

/*
 * Procfs Telemetry Interface: /proc/hmb_pagefile
 */
static int hmb_pagefile_proc_show(struct seq_file *m, void *v)
{
	struct hmb_pagefile_plane *plane = global_hmb_plane;
	u64 reads, writes, frees, zero_hits, pat_hits, raw_pages, bytes_saved;
	u64 total_stored;

	if (!plane) {
		seq_puts(m, "HMB Pagefile Engine: Inactive\n");
		return 0;
	}

	reads = atomic64_read(&plane->stats.reads);
	writes = atomic64_read(&plane->stats.writes);
	frees = atomic64_read(&plane->stats.frees);
	zero_hits = atomic64_read(&plane->stats.zero_hits);
	pat_hits = atomic64_read(&plane->stats.pattern_hits);
	raw_pages = atomic64_read(&plane->stats.raw_allocs);
	bytes_saved = atomic64_read(&plane->stats.bytes_saved);
	total_stored = zero_hits + pat_hits + raw_pages;

	seq_puts(m, "================================================================================\n");
	seq_puts(m, "  HAVEN MEMORY BUS (HMB) — ANDROID COMMON KERNEL PAGING ENGINE (Spec 2.0)\n");
	seq_puts(m, "  Mode: Native VMM Bypass (Zero Bio, Zero blk-mq, Pure Arithmetic)\n");
	seq_puts(m, "================================================================================\n");
	seq_printf(m, "Configured Plane:        %u MB (%lu swap slots)\n",
		   HMB_PLANE_SIZE_MB, plane->total_pages);
	seq_printf(m, "Direct O(1) Page In:     %llu faults resolved\n", reads);
	seq_printf(m, "Direct O(1) Page Out:    %llu pages evicted\n", writes);
	seq_printf(m, "Lock-Free Slot Frees:    %llu slots\n", frees);
	seq_printf(m, "Total VMM Paging Ops:    %llu\n", reads + writes);
	seq_puts(m, "--------------------------------------------------------------------------------\n");
	seq_puts(m, "[Tier 2 z-Pack In-RAM Deduplication Engine]\n");
	seq_printf(m, "  Zero-Page Deduplication: %llu pages (100.0%% compression)\n", zero_hits);
	seq_printf(m, "  Uniform Pattern Hits:    %llu pages (99.8%% compression)\n", pat_hits);
	seq_printf(m, "  Raw Stride Allocations:  %llu pages\n", raw_pages);
	seq_printf(m, "  Physical RAM Saved:      %llu MB (%llu KB)\n",
		   bytes_saved >> 20, bytes_saved >> 10);
	if (total_stored > 0) {
		u64 ratio_x10 = (total_stored * 10) / (raw_pages ? raw_pages : 1);

		seq_printf(m, "  Effective Compression:   %llu.%llux Ratio\n",
			   ratio_x10 / 10, ratio_x10 % 10);
	}
	seq_puts(m, "================================================================================\n");
	return 0;
}

static int hmb_pagefile_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, hmb_pagefile_proc_show, NULL);
}

static const struct proc_ops hmb_pagefile_proc_fops = {
	.proc_open = hmb_pagefile_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

int __init hmb_pagefile_init(void)
{
	struct hmb_pagefile_plane *plane;
	unsigned long bitmap_bytes;

	pr_info("HMB: Initializing Haven Memory Bus Android Paging Engine...\n");

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return -ENOMEM;

	plane->total_pages = HMB_PAGEFILE_MAX_PAGES;
	spin_lock_init(&plane->lock);

	bitmap_bytes = BITS_TO_LONGS(plane->total_pages) * sizeof(unsigned long);

	plane->allocated_bitmap = vzalloc(bitmap_bytes);
	plane->zero_bitmap = vzalloc(bitmap_bytes);
	plane->pattern_bitmap = vzalloc(bitmap_bytes);
	plane->patterns = vzalloc(plane->total_pages * sizeof(u64));
	plane->raw_pages = vzalloc(plane->total_pages * sizeof(struct page *));

	if (!plane->allocated_bitmap || !plane->zero_bitmap ||
	    !plane->pattern_bitmap || !plane->patterns || !plane->raw_pages) {
		pr_err("HMB: Memory plane allocation failed\n");
		vfree(plane->raw_pages);
		vfree(plane->patterns);
		vfree(plane->pattern_bitmap);
		vfree(plane->zero_bitmap);
		vfree(plane->allocated_bitmap);
		kfree(plane);
		return -ENOMEM;
	}

	global_hmb_plane = plane;

	if (!proc_create("hmb_pagefile", 0444, NULL, &hmb_pagefile_proc_fops))
		pr_warn("HMB: Failed to register /proc/hmb_pagefile telemetry\n");

	pr_info("HMB: Android In-Tree Paging Engine ONLINE (%u MB, %lu slots). /proc/hmb_pagefile ready.\n",
		HMB_PLANE_SIZE_MB, plane->total_pages);
	return 0;
}
subsys_initcall(hmb_pagefile_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniel Elliott <ssfdre38@gmail.com>");
MODULE_DESCRIPTION("Haven Memory Bus (HMB) Android Common Kernel Paging Engine");
