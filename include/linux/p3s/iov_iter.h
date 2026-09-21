/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_IOV_ITER_H
#define _LINUX_P3S_IOV_ITER_H

#include <linux/kernel.h>
#include <linux/uio.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/p3s/mm.h>

static inline int want_user_pages_array(struct page ***res, size_t size,
					size_t start, unsigned int maxpages)
{
	unsigned int count = DIV_ROUND_UP(size + start, mm_pte_size(current->mm));

	if (count > maxpages)
		count = maxpages;
	WARN_ON(!count);
	if (!*res) {
		*res = kvmalloc_array(count, sizeof(struct page *), GFP_KERNEL);
		if (!*res)
			return 0;
	}
	return count;
}

static inline int user_iov_npages(const struct iov_iter *i, int maxpages)
{
	size_t skip = i->iov_offset, size = i->count;
	const struct iovec *p;
	int npages = 0;

	for (p = iter_iov(i); size; skip = 0, p++) {
		unsigned long addr = (unsigned long)p->iov_base + skip;
		unsigned long offs = mm_offset_in_page(current->mm, addr);
		size_t len = min(p->iov_len - skip, size);

		if (len) {
			size -= len;
			npages += DIV_ROUND_UP(offs + len, mm_pte_size(current->mm));
			if (unlikely(npages > maxpages))
				return maxpages;
		}
	}
	return npages;
}

#endif /* _LINUX_P3S_IOV_ITER_H */
