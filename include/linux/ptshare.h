/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PTSHARE_H
#define _LINUX_PTSHARE_H

#include <linux/mm.h>
#include <linux/refcount.h>

struct mm_struct;

struct ptshare_desc {
	/* The headless shadow MM for this domain */
	struct mm_struct *ptshare_mm;
	refcount_t refcount;
};

#ifdef CONFIG_PTSHARE
static inline bool vma_shares_pagetables(const struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_PT_SHARED;
}
#else
static inline bool vma_shares_pagetables(const struct vm_area_struct *vma)
{
	return false;
}
#endif

#endif /* _LINUX_PTSHARE_H */
