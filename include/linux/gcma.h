/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __GCMA_H__
#define __GCMA_H__

#include <linux/types.h>

#ifdef CONFIG_GCMA
<<<<<<< HEAD   (d1c52ce211834c06e70f38c6832a6cba932b5edb UPSTREAM: tls: always refresh the queue when reading sock)
||||||| BASE   (830a2dadaa8fc019f8238b1adc292b5de1e7eea5 ANDROID: GKI: Add empty symbol list for meizu)
enum gcma_stat_type {
	ALLOCATED_PAGE,
	STORED_PAGE,
	LOADED_PAGE,
	EVICTED_PAGE,
	CACHED_PAGE,
	DISCARDED_PAGE,
	TOTAL_PAGE,
	NUM_OF_GCMA_STAT,
};

#ifdef CONFIG_GCMA_SYSFS
u64 gcma_stat_get(enum gcma_stat_type type);
#else
static inline u64 gcma_stat_get(enum gcma_stat_type type) { return 0; }
#endif

=======
enum gcma_stat_type {
	ALLOCATED_PAGE,
	STORED_PAGE,
	LOADED_PAGE,
	EVICTED_PAGE,
	CACHED_PAGE,
	DISCARDED_PAGE,
	TOTAL_PAGE,
	NUM_OF_GCMA_STAT,
};

#ifdef CONFIG_GCMA_SYSFS
u64 gcma_stat_get(enum gcma_stat_type type);
#else
static inline u64 gcma_stat_get(enum gcma_stat_type type) { return 0; }
#endif

/*
 * NOTE: allocated pages are still marked reserved and when freeing them
 * the caller should ensure they are isolated and not referenced by anyone
 * other than the caller.
 */
>>>>>>> CHANGE (3f3a5c5e782d183bab668b1bd992e6812494f798 ANDROID: ensure pages allocated from GCMA are correctly refc)
extern void gcma_alloc_range(unsigned long start_pfn, unsigned long end_pfn);
extern void gcma_free_range(unsigned long start_pfn, unsigned long end_pfn);
extern int register_gcma_area(const char *name, phys_addr_t base,
				phys_addr_t size);
#else
static inline void gcma_alloc_range(unsigned long start_pfn,
				    unsigned long end_pfn) {}
static inline void gcma_free_range(unsigned long start_pfn,
				   unsigned long end_pfn) {}
static inline int register_gcma_area(const char *name, phys_addr_t base,
				     phys_addr_t size) { return -EINVAL; }
#endif

#endif
