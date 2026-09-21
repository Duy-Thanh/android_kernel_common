/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_SWAP_H
#define _LINUX_P3S_SWAP_H

#include <linux/types.h>
#include <linux/swap.h>
#include <linux/sort.h>
#include <linux/p3s/const.h>
#include <linux/p3s/mm.h>

#if defined(CONFIG_ARM64_PER_PROCESS_PAGE_SIZE) && defined(CONFIG_SWAP)

/*
 * Detect swap header geometry. Even though standard Android P3S deployments
 * run only user application processes in 4KB mode and system daemons format
 * swap using the native 16KB kernel page size, supporting 4KB swap headers
 * is valuable to enable running entire userspace test environments in 4KB mode
 * for full-system stability testing.
 */
static inline unsigned int p3s_swap_header_page_shift(const union swap_header *swap_header)
{
	const char *header = (const char *)swap_header;

	if (!memcmp("SWAPSPACE2", header + PAGE_SIZE_KERNEL - 10, 10))
		return PAGE_SHIFT_KERNEL;

	/*
	 * A 4KB mkswap process formats the area in 4KB process-page units.
	 * The swapon caller need not be that process, so inspect the 4KB
	 * location independently of the caller's current page size.
	 */
	if (!memcmp("SWAPSPACE2", header + PAGE_SIZE_4KB - 10, 10))
		return PAGE_SHIFT_4KB;

	return PAGE_SHIFT_KERNEL;
}

static inline unsigned long p3s_swap_header_max_badpages(const union swap_header *swap_header)
{
	unsigned int header_page_shift = p3s_swap_header_page_shift(swap_header);

	return ((1UL << header_page_shift) - 10 -
		offsetof(union swap_header, info.badpages)) /
	       sizeof(swap_header->info.badpages[0]);
}

static inline char *p3s_swap_header_magic(const union swap_header *swap_header)
{
	return (char *)swap_header +
	       (1UL << p3s_swap_header_page_shift(swap_header)) - 10;
}

static inline int p3s_swap_badpage_cmp(const void *left, const void *right)
{
	const __u32 left_page = *(const __u32 *)left;
	const __u32 right_page = *(const __u32 *)right;

	return (left_page > right_page) - (left_page < right_page);
}

static inline int p3s_convert_swap_header(union swap_header *swap_header)
{
	unsigned int header_page_shift = p3s_swap_header_page_shift(swap_header);
	unsigned int page_shift_delta = PAGE_SHIFT_KERNEL - header_page_shift;
	unsigned int nr_badpages = swap_header->info.nr_badpages;
	unsigned int i, nr_unique = 0;
	u64 nr_pages;

	if (!page_shift_delta)
		return 0;

	nr_pages = ((u64)swap_header->info.last_page + 1) >> page_shift_delta;
	if (!nr_pages) {
		pr_warn("Empty swap-file\n");
		return -EINVAL;
	}

	swap_header->info.last_page = nr_pages - 1;
	for (i = 0; i < nr_badpages; i++)
		swap_header->info.badpages[i] >>= page_shift_delta;

	sort(swap_header->info.badpages, nr_badpages,
	     sizeof(swap_header->info.badpages[0]), p3s_swap_badpage_cmp, NULL);
	for (i = 0; i < nr_badpages; i++) {
		if (nr_unique && swap_header->info.badpages[i] ==
				 swap_header->info.badpages[nr_unique - 1])
			continue;
		swap_header->info.badpages[nr_unique++] =
			swap_header->info.badpages[i];
	}
	swap_header->info.nr_badpages = nr_unique;
	return 0;
}

#else /* !(CONFIG_ARM64_PER_PROCESS_PAGE_SIZE && CONFIG_SWAP) */

static inline unsigned long p3s_swap_header_max_badpages(const union swap_header *swap_header)
{
	return MAX_SWAP_BADPAGES;
}

static inline char *p3s_swap_header_magic(const union swap_header *swap_header)
{
	return (char *)swap_header->magic.magic;
}

static inline int p3s_convert_swap_header(union swap_header *swap_header)
{
	return 0;
}

#endif /* CONFIG_ARM64_PER_PROCESS_PAGE_SIZE && CONFIG_SWAP */

#endif /* _LINUX_P3S_SWAP_H */
