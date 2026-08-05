/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PTSHARE_H
#define _LINUX_PTSHARE_H

#include <linux/mm.h>
#include <uapi/asm-generic/mman-common.h>
#include <linux/refcount.h>

struct mm_struct;

struct ptshare_desc {
	/* The headless shadow MM for this domain */
	struct mm_struct *ptshare_mm;
	refcount_t refcount;
	struct rcu_head rcu;
};

#ifdef CONFIG_PTSHARE
static inline bool vma_shares_pagetables(const struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_PT_SHARED;
}

static inline struct ptshare_desc *vma_ptshare_desc(const struct vm_area_struct *vma)
{
	/*
	 * Shadow VMAs belong to a ptshare_mm, and do NOT have VM_PT_SHARED set.
	 * Client VMAs are those that have VM_PT_SHARED set.
	 */
	VM_BUG_ON_VMA(!vma_shares_pagetables(vma), vma);

	if (unlikely(!vma_shares_pagetables(vma)))
		return NULL;

	return vma->__vm_ptshare_desc;
}

static inline void vma_set_ptshare_desc(struct vm_area_struct *vma,
					struct ptshare_desc *desc)
{
	VM_BUG_ON_VMA(!vma_shares_pagetables(vma), vma);

	if (unlikely(!vma_shares_pagetables(vma)))
		return;

	vma->__vm_ptshare_desc = desc;
}

static inline refcount_t *vma_ptshare_refcount(struct vm_area_struct *vma)
{
	/* Shadow VMAs are in a ptshare_mm and do NOT have VM_PT_SHARED set */
	VM_BUG_ON_VMA(vma_shares_pagetables(vma), vma);

	if (unlikely(vma_shares_pagetables(vma)))
		return NULL;

	return &vma->__vm_ptshare_refcount;
}

static inline const refcount_t *vma_ptshare_refcount_const(const struct vm_area_struct *vma)
{
	/* Shadow VMAs are in a ptshare_mm and do NOT have VM_PT_SHARED set */
	VM_BUG_ON_VMA(vma_shares_pagetables(vma), vma);

	if (unlikely(vma_shares_pagetables(vma)))
		return NULL;

	return &vma->__vm_ptshare_refcount;
}

static inline bool mm_is_ptshare_shadow(const struct mm_struct *mm)
{
	return mm_flags_test(MMF_PTSHARE_SHADOW, mm);
}

int ptshare_get_vma(struct vm_area_struct *vma, struct ptshare_desc *desc);
void ptshare_put_vma(struct vm_area_struct *vma, struct ptshare_desc *desc);

unsigned long ptshare_install_vma(struct mm_struct *mm, unsigned long addr,
				  unsigned long len, unsigned long vm_flags);

struct ptshare_desc *ptshare_alloc_desc(void);
void ptshare_put_desc(struct ptshare_desc *desc);
void ptshare_get_desc(struct ptshare_desc *desc);
void ptshare_free_desc_rcu(struct ptshare_desc *desc);
void ptshare_exit(struct mm_struct *mm);
int ptshare_fork_vma(struct mm_struct *mm, struct vm_area_struct *vma);
/**
 * ptdesc_get - Increment reference count on a page table descriptor
 * @pt: The page table descriptor.
 */
static inline void ptdesc_get(struct ptdesc *pt)
{
	ptdesc_pmd_pts_inc(pt);
}

/**
 * ptdesc_put - Decrement reference count on a page table descriptor
 * @pt: The page table descriptor.
 *
 * Note: This only decrements the sharing count and does not free the page
 * table page. The lifecycle of the page table page is managed by the
 * ptshare infrastructure (which frees it when the shared mappings are torn down).
 */
static inline void ptdesc_put(struct ptdesc *pt)
{
	ptdesc_pmd_pts_dec(pt);
}

/**
 * ptdesc_refcount - Return the reference count on a page table descriptor
 * @pt: The page table descriptor.
 */
static inline int ptdesc_refcount(struct ptdesc *pt)
{
	return ptdesc_pmd_pts_count(pt);
}

int ptshare_unshare_vma_locked(struct vm_area_struct *vma);
int ptshare_unshare_vma_on_gup(struct mm_struct *mm, unsigned long addr,
			       int *locked, bool unlockable);

static inline struct page *ptshare_get_user_page_vma_remote(struct mm_struct *mm,
						    unsigned long addr,
						    int gup_flags,
						    struct vm_area_struct **vmap)
{
	struct page *page;
	struct vm_area_struct *vma;
	unsigned long old_start, old_end, old_pgoff;
	struct file *old_file;
	int got;
	int locked;

	if (WARN_ON_ONCE(unlikely(gup_flags & FOLL_NOWAIT)))
		return ERR_PTR(-EINVAL);

retry:
	locked = 1;
	vma = vma_lookup(mm, addr);
	if (!vma)
		return ERR_PTR(-EFAULT);

	old_start = vma->vm_start;
	old_end = vma->vm_end;
	old_file = vma->vm_file;
	old_pgoff = vma->vm_pgoff;

	got = get_user_pages_remote(mm, addr, 1, gup_flags, &page, &locked);

	if (got < 0) {
		if (!locked)
			mmap_read_lock(mm);
		return ERR_PTR(got);
	}

	if (!locked) {
		mmap_read_lock(mm);
		vma = vma_lookup(mm, addr);
		if (!vma ||
		    vma->vm_start != old_start ||
		    vma->vm_end != old_end ||
		    vma->vm_file != old_file ||
		    vma->vm_pgoff != old_pgoff) {
			/* VMA changed while lock was dropped. Retry. */
			put_page(page);
			goto retry;
		}
	}

	*vmap = vma;
	return page;
}

static inline bool ptshare_behavior_needs_unshare(int behavior)
{
	return behavior == MADV_DONTNEED ||
	       behavior == MADV_DONTNEED_LOCKED ||
	       behavior == MADV_FREE;
}

#else /* !CONFIG_PTSHARE */
static inline bool vma_shares_pagetables(const struct vm_area_struct *vma)
{
	return false;
}

static inline struct ptshare_desc *vma_ptshare_desc(const struct vm_area_struct *vma)
{
	return NULL;
}

static inline void vma_set_ptshare_desc(struct vm_area_struct *vma,
					struct ptshare_desc *desc)
{
}

static inline refcount_t *vma_ptshare_refcount(struct vm_area_struct *vma)
{
	return NULL;
}

static inline const refcount_t *vma_ptshare_refcount_const(const struct vm_area_struct *vma)
{
	return NULL;
}

static inline bool mm_is_ptshare_shadow(const struct mm_struct *mm)
{
	return false;
}

static inline int ptshare_get_vma(struct vm_area_struct *vma,
					struct ptshare_desc *desc)
{
	return 0;
}

static inline void ptshare_put_vma(struct vm_area_struct *vma,
					struct ptshare_desc *desc)
{
}

static inline unsigned long ptshare_install_vma(struct mm_struct *mm,
						unsigned long addr,
						unsigned long len,
						unsigned long vm_flags)
{
	return addr;
}

static inline struct ptshare_desc *ptshare_alloc_desc(void)
{
	return NULL;
}

static inline void ptshare_put_desc(struct ptshare_desc *desc)
{
}

static inline void ptshare_get_desc(struct ptshare_desc *desc)
{
}

static inline void ptshare_free_desc_rcu(struct ptshare_desc *desc)
{
}

static inline void ptshare_exit(struct mm_struct *mm)
{
}

static inline int ptshare_fork_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	return 0;
}

static inline void ptdesc_get(struct ptdesc *pt)
{
}

static inline void ptdesc_put(struct ptdesc *pt)
{
}

static inline int ptdesc_refcount(struct ptdesc *pt)
{
	return 0;
}

static inline int ptshare_unshare_vma_locked(struct vm_area_struct *vma)
{
	return 0;
}

static inline int ptshare_unshare_vma_on_gup(struct mm_struct *mm, unsigned long addr,
					     int *locked, bool unlockable)
{
	return 0;
}

static inline struct page *ptshare_get_user_page_vma_remote(struct mm_struct *mm,
						    unsigned long addr,
						    int gup_flags,
						    struct vm_area_struct **vmap)
{
	return get_user_page_vma_remote(mm, addr, gup_flags, vmap);
}

static inline bool ptshare_behavior_needs_unshare(int behavior)
{
	return false;
}

#endif /* CONFIG_PTSHARE */

#endif /* _LINUX_PTSHARE_H */
