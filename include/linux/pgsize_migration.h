/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PAGE_SIZE_MIGRATION_H
#define _LINUX_PAGE_SIZE_MIGRATION_H

/*
 * include/linux/pgsize_migration.h
 *
 * Page Size Migration
 *
 * Copyright (c) 2024, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@goole.com>
 *
 * This file contains the APIs for mitigations to ensure
 * app compatibility during the transition from 4kB to 16kB
 * page size in Android.
 */

#include <linux/mm.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>

/*
 * vm_flags representation of VMA padding pages.
 *
 *
 * 4 high bits of vm_flags [62,59] (63 is used for page size emulation in x86_64)
 * are used to represent ELF segment padding up to 60kB, which is sufficient for
 * ELFs of both 16kB and 64kB segment alignment (p_align).
 *
 * The representation is illustrated below.
 *
 *                    62        61        60        59
 *                _________ _________ _________ _________
 *               |  Bit 2  |  Bit 1  |  Bit 2  |  Bit 1  |
 *               | of 16kB | of 16kB | of  4kB | of  4kB |
 *               |  chunks |  chunks |  chunks |  chunks |
 *               |_________|_________|_________|_________|
 */

#define VM_PAD_16KB_BIT2	(_AC(1,ULL) << 62)
#define VM_PAD_16KB_BIT1	(_AC(1,ULL) << 61)
#define VM_PAD_4KB_BIT2		(_AC(1,ULL) << 60)
#define VM_PAD_4KB_BIT1		(_AC(1,ULL) << 59)
#define VM_PAD_BITS		(VM_PAD_16KB_BIT2|VM_PAD_16KB_BIT1|VM_PAD_4KB_BIT2|VM_PAD_4KB_BIT1)
#define VM_TOTAL_PAD_PAGES 	15

typedef void (*show_map_vma_fn)(struct seq_file *m, struct vm_area_struct *vma);

#if PAGE_SIZE == SZ_4K
extern void vma_set_pad_pages(struct vm_area_struct *vma,
			      unsigned long nr_pages);

extern unsigned long vma_pad_pages(struct vm_area_struct *vma);

extern void madvise_vma_pad_pages(struct vm_area_struct *vma,
				  unsigned long start, unsigned long end);

extern  void show_map_vma_pad(struct vm_area_struct *vma,
				    show_map_vma_fn func,
				    struct seq_file *m);

extern const char *vma_pad_name(struct vm_area_struct *vma);
#else /* PAGE_SIZE != SZ_4K */
static inline void vma_set_pad_pages(struct vm_area_struct *vma,
				     unsigned long nr_pages)
{
}

static inline unsigned long vma_pad_pages(struct vm_area_struct *vma)
{
}

static inline void madvise_vma_pad_pages(struct vm_area_struct *vma,
					 unsigned long start, unsigned long end)
{
}

static inline void show_map_vma_pad(struct vm_area_struct *vma,
				    show_map_vma_fn func,
				    struct seq_file *m)
{
}

static inline const char *vma_pad_name(struct vm_area_struct *vma)
{
	return NULL;
}
#endif /* PAGE_SIZE == SZ_4K */

static inline unsigned long vma_data_pages(struct vm_area_struct *vma)
{
	return vma_pages(vma) - vma_pad_pages(vma);
}
#endif /* _LINUX_PAGE_SIZE_MIGRATION_H */
