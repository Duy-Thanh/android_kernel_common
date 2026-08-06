/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PTSHARE_H
#define _LINUX_PTSHARE_H

#include <linux/mm.h>
#include <uapi/asm-generic/mman-common.h>
#include <linux/refcount.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>

struct mm_struct;
struct mem_cgroup;

struct ptshare_desc {
	/* The headless shadow MM for this domain */
	struct mm_struct *ptshare_mm;
	struct mmu_notifier mmu_notifier;
	struct mem_cgroup *memcg;
	refcount_t refcount;
	struct rcu_head rcu;
};

#ifdef CONFIG_PTSHARE
static inline bool vma_shares_pagetables(const struct vm_area_struct *vma)
{
	return (vma->vm_flags & VM_PT_SHARED) &&
		READ_ONCE(vma->__vm_ptshare_desc) != NULL;
}

static inline bool mm_is_ptshare_shadow(const struct mm_struct *mm)
{
	return mm_flags_test(MMF_PTSHARE_SHADOW, mm);
}

static inline bool vma_is_ptshare_shadow(const struct vm_area_struct *vma)
{
	return vma->vm_mm && mm_is_ptshare_shadow(vma->vm_mm);
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

	return READ_ONCE(vma->__vm_ptshare_desc);
}

static inline void vma_set_ptshare_desc(struct vm_area_struct *vma,
					struct ptshare_desc *desc)
{
	VM_BUG_ON_VMA(!(vma->vm_flags & VM_PT_SHARED), vma);

	/*
	 * Do not use vma_shares_pagetables() here because it checks if the
	 * descriptor is non-NULL. During initial installation, the descriptor
	 * starts as NULL, so vma_shares_pagetables() would return false and
	 * prevent us from ever setting it.
	 */
	if (unlikely(!(vma->vm_flags & VM_PT_SHARED)))
		return;

	WRITE_ONCE(vma->__vm_ptshare_desc, desc);
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



static inline bool mm_shares_pagetables(struct mm_struct *mm)
{
	return rcu_access_pointer(mm->ptshare_desc) != NULL;
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

#ifdef CONFIG_MEMCG
struct mem_cgroup *ptshare_shadow_memcg(const struct mm_struct *mm);

/*
 * Page table allocations use alloc_pages(__GFP_ACCOUNT), which charges
 * current->active_memcg instead of passing the mm_struct. To ensure page
 * tables allocated for the shadow MM are charged to the creator's memcg
 * rather than the faulting client task, shadow MM allocation paths use
 * ptshare_bind_active_memcg() to set the active remote memcg.
 */
static inline struct mem_cgroup *
ptshare_bind_active_memcg(struct mm_struct *mm)
{
	struct mem_cgroup *memcg = ptshare_shadow_memcg(mm);
	struct mem_cgroup *old_memcg;

	if (!memcg)
		return NULL;

	old_memcg = set_active_memcg(memcg);
	mem_cgroup_put(memcg);
	return old_memcg;
}

static inline void
ptshare_unbind_active_memcg(struct mem_cgroup *old_memcg)
{
	if (old_memcg)
		set_active_memcg(old_memcg);
}
#else
static inline struct mem_cgroup *ptshare_shadow_memcg(const struct mm_struct *mm)
{
	return NULL;
}
#endif
int ptshare_unshare_vma_locked(struct vm_area_struct *vma);
int ptshare_unshare_vma_on_gup(struct mm_struct *mm, unsigned long addr,
			       int *locked, bool unlockable);
int ptshare_validate_mmap(struct file *file, unsigned long addr,
			  unsigned long len, unsigned long prot,
			  unsigned long flags, vm_flags_t vm_flags,
			  unsigned long pgoff);

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

struct zap_details;

vm_fault_t ptshare_handle_mm_fault(struct vm_fault *vmf);

bool ptshare_vma_skip_zap_pte_range(struct vm_area_struct *vma,
				    struct zap_details *details);

bool ptshare_free_pte_range(struct mmu_gather *tlb, pgtable_t ptdesc,
			   unsigned long addr);

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

static inline bool mm_is_ptshare_shadow(const struct mm_struct *mm)
{
	return false;
}

static inline bool vma_is_ptshare_shadow(const struct vm_area_struct *vma)
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



static inline bool mm_shares_pagetables(struct mm_struct *mm)
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

static inline struct mem_cgroup *ptshare_shadow_memcg(const struct mm_struct *mm)
{
	return NULL;
}

static inline struct mem_cgroup *
ptshare_bind_active_memcg(struct mm_struct *mm)
{
	return NULL;
}

static inline void
ptshare_unbind_active_memcg(struct mem_cgroup *old_memcg)
{
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

static inline int ptshare_validate_mmap(struct file *file, unsigned long addr,
					unsigned long len, unsigned long prot,
					unsigned long flags, vm_flags_t vm_flags,
					unsigned long pgoff)
{
	return 0;
}

static inline vm_fault_t ptshare_handle_mm_fault(struct vm_fault *vmf)
{
	return VM_FAULT_SIGBUS;
}

static inline bool ptshare_vma_skip_zap_pte_range(struct vm_area_struct *vma,
						  struct zap_details *details)
{
	return false;
}

static inline bool ptshare_free_pte_range(struct mmu_gather *tlb,
					  pgtable_t ptdesc, unsigned long addr)
{
	return false;
}

static inline bool ptshare_behavior_needs_unshare(int behavior)
{
	return false;
}

#endif /* CONFIG_PTSHARE */

#endif /* _LINUX_PTSHARE_H */
