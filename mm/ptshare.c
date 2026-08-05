// SPDX-License-Identifier: GPL-2.0
/*
 * Shared Page Table Support
 *
 * The ptshare infrastructure provides a way for processes to share
 * page tables (PTE-level) for non-writable (and therefore
 * non-CoWable) file-backed mappings.
 * This is achieved by using a headless shadow MM (ptshare_mm) to
 * host the shared page tables.
 *
 * Copyright (c) 2026, Google LLC.
 * Author: Kalesh Singh <kaleshsingh@google.com>
 */
#include <linux/ptshare.h>
#include <linux/mm_types.h>
#include <linux/file.h>
#include <linux/mmap_lock.h>
#include <linux/mempolicy.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/shmem_fs.h>

static inline void mmap_lock_set_ptshare_class(struct mm_struct *mm)
{
	static struct lock_class_key ptshare_mmap_lock_key;

	lockdep_set_class(&mm->mmap_lock, &ptshare_mmap_lock_key);
}

struct ptshare_desc *ptshare_alloc_desc(void)
{
	struct ptshare_desc *desc;

	desc = kzalloc_obj(*desc, GFP_KERNEL);
	if (!desc)
		return NULL;

	desc->ptshare_mm = mm_alloc();
	if (!desc->ptshare_mm) {
		kfree(desc);
		return NULL;
	}
	mmgrab(desc->ptshare_mm);
	mm_flags_test_and_set(MMF_PTSHARE_SHADOW, desc->ptshare_mm);

#ifdef CONFIG_MEMCG
	/* Headless shadow MM does not have an owner task */
	RCU_INIT_POINTER(desc->ptshare_mm->owner, NULL);
#endif

	/* Shadow MM is also marked as shadow by pointing its desc back to itself */
	rcu_assign_pointer(desc->ptshare_mm->ptshare_desc, desc);

	mmap_lock_set_ptshare_class(desc->ptshare_mm);

	refcount_set(&desc->refcount, 1);

	return desc;
}

/*
 * Once the descriptor's refcount reaches zero, the current thread
 * has exclusive ownership. This occurs when the last process (mm_struct)
 * participating in the sharing domain has exited and released its reference.
 *
 * Since new processes can only join the domain by inheriting it across
 * fork() (which requires an active mm_struct reference), no new references
 * can be created once the count reaches zero.
 *
 * Therefore, no further synchronization is needed while we tear down
 * the internal state.
 *
 * Use mmput_async() to avoid synchronous lockdep issues when called
 * from mmap_lock held contexts.
 */
void ptshare_put_desc(struct ptshare_desc *desc)
{
	if (!desc)
		return;

	if (refcount_dec_and_test(&desc->refcount))
		mmput_async(desc->ptshare_mm);
}

void ptshare_get_desc(struct ptshare_desc *desc)
{
	if (desc)
		refcount_inc(&desc->refcount);
}

static void ptshare_desc_rcu_free(struct rcu_head *head)
{
	struct ptshare_desc *desc = container_of(head, struct ptshare_desc, rcu);
	struct mm_struct *mm = desc->ptshare_mm;

	kfree(desc);
	mmdrop_async(mm);
}

void ptshare_free_desc_rcu(struct ptshare_desc *desc)
{
	call_rcu(&desc->rcu, ptshare_desc_rcu_free);
}

int ptshare_validate_mmap(struct file *file, unsigned long addr,
			  unsigned long len, unsigned long prot,
			  unsigned long flags, vm_flags_t vm_flags,
			  unsigned long pgoff)
{
	struct vm_area_struct *vma;
	struct ptshare_desc *desc;

	if (!(vm_flags & VM_PT_SHARED))
		return 0;

	/* Only support file-backed mappings */
	if (!file)
		return -EINVAL;

	/* Shmem/tmpfs mappings are not allowed */
	if (shmem_file(file))
		return -EINVAL;

	/* Exclude special device files (char/block devices) */
	if (unlikely(S_ISCHR(file_inode(file)->i_mode) || S_ISBLK(file_inode(file)->i_mode)))
		return -EINVAL;

	/* Writable mappings are not allowed */
	if (unlikely(prot & PROT_WRITE))
		return -EINVAL;

	/* Non-duplicable mappings (VM_DONTCOPY) are not allowed */
	if (unlikely(vm_flags & VM_DONTCOPY))
		return -EINVAL;

	/* mlocked mappings (MAP_LOCKED) are not allowed */
	if (unlikely((flags & MAP_LOCKED) ||
		     (vm_flags & (VM_LOCKED | VM_LOCKONFAULT))))
		return -EINVAL;

	/*
	 * Precise address control is required to guarantee the
	 * mapping covers the entire PMD range.
	 */
	if (unlikely(!(flags & MAP_FIXED)))
		return -EINVAL;

	/* Require PMD alignment of both base and size */
	if (unlikely(!IS_ALIGNED(addr, PMD_SIZE) ||
		     !IS_ALIGNED(len, PMD_SIZE)))
		return -EINVAL;

	desc = rcu_dereference_protected(current->mm->ptshare_desc,
					 lockdep_is_held(&current->mm->mmap_lock));
	if (desc) {
		/*
		 * Child processes that inherited the VMAs but not the creator
		 * status are not allowed to map new VMAs into the domain.
		 */
		if (!mm_flags_test(MMF_PTSHARE_CREATOR, current->mm))
			return -EPERM;
	} else {
		/*
		 * If the caller doesn't have a ptshare_desc, allocate it now.
		 */
		desc = ptshare_alloc_desc();
		if (!desc)
			return -ENOMEM;

		rcu_assign_pointer(current->mm->ptshare_desc, desc);
		mm_flags_test_and_set(MMF_PTSHARE_CREATOR, current->mm);

		/*
		 * Since we were not previously part of any shared page table
		 * domain, we can allow the mapping without further checks.
		 */
		return 0;
	}

	/* The shared MM should never be the caller of this syscall */
	if (WARN_ON_ONCE(current->mm == desc->ptshare_mm))
		return -EINVAL;

	/*
	 * Check for virtual address space collisions in the shared manager.
	 * Overlapping shared mappings are not allowed. MAP_FIXED is not allowed
	 * to overwrite an existing shared VMA in the domain, to prevent
	 * cross-process address space corruption and simplify lifecycle tracking.
	 */
	mmap_read_lock(desc->ptshare_mm);
	vma = find_vma_intersection(desc->ptshare_mm, addr, addr + len);
	mmap_read_unlock(desc->ptshare_mm);
	if (vma)
		return -EINVAL;

	return 0;
}

int ptshare_unshare_vma_locked(struct vm_area_struct *vma)
{
	struct mm_struct *mm = vma->vm_mm;
	unsigned long addr;
	struct mmu_notifier_range range;

	mmap_assert_write_locked(mm);

	if (!vma_shares_pagetables(vma))
		return 0;

	if (WARN_ON_ONCE(!IS_ALIGNED(vma->vm_start, PMD_SIZE)) ||
	    WARN_ON_ONCE(!IS_ALIGNED(vma->vm_end, PMD_SIZE)))
		return -EINVAL;

	vma_start_write(vma);

	/*
	 * Notify secondary MMUs (like KVM or IOMMUs) that we are clearing these
	 * page table entries. This ensures they invalidate their shadow mappings
	 * and don't continue to access the shared page tables stale.
	 */
	mmu_notifier_range_init(&range, MMU_NOTIFY_CLEAR, 0, mm,
				vma->vm_start, vma->vm_end);
	mmu_notifier_invalidate_range_start(&range);

	addr = vma->vm_start;
	while (addr < vma->vm_end) {
		pgd_t *pgd;
		p4d_t *p4d;
		pud_t *pud;
		pmd_t *pmd;

		pgd = pgd_offset(mm, addr);
		if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd))) {
			addr = pgd_addr_end(addr, vma->vm_end);
			continue;
		}
		p4d = p4d_offset(pgd, addr);
		if (p4d_none(*p4d) || unlikely(p4d_bad(*p4d))) {
			addr = p4d_addr_end(addr, vma->vm_end);
			continue;
		}
		pud = pud_offset(p4d, addr);
		if (pud_none(*pud) || unlikely(pud_bad(*pud))) {
			addr = pud_addr_end(addr, vma->vm_end);
			continue;
		}
		pmd = pmd_offset(pud, addr);
		if (pmd_none(*pmd)) {
			addr += PMD_SIZE;
			continue;
		}

		{
			spinlock_t *ptl;
			pte_t *pte;

			/*
			 * We hold mmap_lock write lock, which serializes us with other
			 * structural page table writers (e.g. munmap, mprotect) in this MM.
			 *
			 * However, we must still serialize with concurrent readers/walkers
			 * (like rmap walkers or page faults) who only hold the mmap_lock
			 * for reading and could be actively walking the PTEs.
			 *
			 * Locking the PTL (PTE page lock) ensures that any active walker
			 * on this range either finishes before we clear the PMD, or
			 * detects the cleared PMD and aborts after we release the lock.
			 */
			pte = pte_offset_map_lock(mm, pmd, addr, &ptl);
			if (pte) {
				pmd_t pmdval = *pmd;
				struct ptdesc *ptdesc;

				if (WARN_ON_ONCE(pmd_trans_huge(pmdval))) {
					pte_unmap_unlock(pte, ptl);
					addr += PMD_SIZE;
					continue;
				}

				ptdesc = page_ptdesc(pmd_page(pmdval));
				pmd_clear(pmd);
				mm_dec_nr_ptes(mm);
				pte_unmap_unlock(pte, ptl);

				ptdesc_put(ptdesc);
			}
		}
		addr += PMD_SIZE;
	}

	/*
	 * We cleared the PMDs but we don't free the page table pages here
	 * (they are owned by shadow MM). So we don't need mmu_gather.
	 *
	 * We must flush the TLB of the client MM before releasing the VMA
	 * reference to ensure no CPUs hold stale TLB entries.
	 *
	 * GUP-fast (lockless page table walkers) are safe because:
	 * 1. GUP-fast disables interrupts (acting as RCU read lock).
	 * 2. The page table page is eventually freed by shadow MM using
	 *    mmu_gather with RCU-delayed free (call_rcu).
	 * So the page table page won't be freed until GUP-fast finishes.
	 *
	 * Furthermore, GUP-fast will not get wrong/corrupted pages:
	 * Since we only detach the page table page without modifying its
	 * PTEs, any in-flight GUP-fast walker that read the PMD before it
	 * was cleared will still resolve to the correct, unmodified
	 * read-only file pages. GUP-fast with write intent will naturally
	 * fail on the read-only PTEs and fallback to the slow path.
	 */
	flush_tlb_range(vma, vma->vm_start, vma->vm_end);

	mmu_notifier_invalidate_range_end(&range);

	ptshare_put_vma(vma, vma_ptshare_desc(vma));
	vm_flags_clear(vma, VM_PT_SHARED);

	return 0;
}

/*
 * ptshare_dup_vma - Create a new shadow VMA for the ptshare_mm.
 *
 * Since we are cloning a client VMA, we must ensure it is isolated
 * from the client's locking structures, and process-specific metadata.
 */
static inline struct vm_area_struct *ptshare_dup_vma(struct vm_area_struct *vma,
						 struct mm_struct *ptshare_mm)
{
	struct vm_area_struct *ptshare_vma = vm_area_dup(vma);

	if (!ptshare_vma)
		return NULL;

	if (vma_dup_policy(vma, ptshare_vma)) {
		vm_area_free(ptshare_vma);
		return NULL;
	}

	/* Set the shared mm for the ptshare shadow VMA */
	ptshare_vma->vm_mm = ptshare_mm;

	/*
	 * Clear the shared page table flag, this will be used by
	 * rmap walks to determine that ptshare_mm VMAs are candidate
	 * for rmap lookups.
	 *
	 * Also clear VM_ACCOUNT to prevent double accounting and
	 * committed memory underflow during teardown.
	 *
	 * Use lockless vm_flags_init() to avoid corrupting VMA lock state
	 * of the newly duplicated VMA.
	 */
	vm_flags_init(ptshare_vma, vma->vm_flags &
			~(VM_PT_SHARED | VM_ACCOUNT | VM_LOCKED | VM_LOCKONFAULT));

	/* Initialize the refcount for this shared VMA */
	refcount_set(vma_ptshare_refcount(ptshare_vma), 1);

	return ptshare_vma;
}

static inline int __ptshare_install_vma(struct vm_area_struct *vma)
{
	unsigned long len = vma->vm_end - vma->vm_start;
	unsigned long addr = vma->vm_start;
	struct vm_area_struct *ptshare_vma;
	struct ptshare_desc *desc = vma_ptshare_desc(vma);
	struct mm_struct *ptshare_mm;
	int ret = 0;

	if (unlikely(!desc))
		return -EINVAL;

	ptshare_mm = desc->ptshare_mm;

	mmap_assert_write_locked(vma->vm_mm);
	mmap_write_lock(ptshare_mm);

	/* Re-check for overlap while holding the write lock */
	ptshare_vma = find_vma_intersection(ptshare_mm, addr, addr + len);
	if (ptshare_vma) {
		ret = -EINVAL;
		goto unlock;
	}

	ptshare_vma = ptshare_dup_vma(vma, ptshare_mm);
	if (!ptshare_vma) {
		ret = -ENOMEM;
		goto unlock;
	}

	/*
	 * Increment the file reference count and notify any device-specific
	 * VMA open hooks to ensure the shared VMA is properly tracked.
	 */
	if (ptshare_vma->vm_file)
		get_file(ptshare_vma->vm_file);

	if (ptshare_vma->vm_ops && ptshare_vma->vm_ops->open)
		ptshare_vma->vm_ops->open(ptshare_vma);

	ret = insert_vm_struct(ptshare_mm, ptshare_vma);
	if (ret)
		goto put_file;

	vm_stat_account(ptshare_mm, ptshare_vma->vm_flags, vma_pages(ptshare_vma));
	goto unlock;

put_file:
	if (ptshare_vma->vm_ops && ptshare_vma->vm_ops->close)
		ptshare_vma->vm_ops->close(ptshare_vma);

	if (ptshare_vma->vm_file)
		fput(ptshare_vma->vm_file);

	mpol_put(vma_policy(ptshare_vma));

	vm_area_free(ptshare_vma);

unlock:
	mmap_write_unlock(ptshare_mm);

	return ret;
}

unsigned long ptshare_install_vma(struct mm_struct *mm, unsigned long addr,
				 unsigned long len, unsigned long vm_flags)
{
	int install_err, unmap_err;
	struct vm_area_struct *vma;
	struct ptshare_desc *desc;
	int err = -EINVAL;

	mmap_assert_write_locked(mm);

	if (!(vm_flags & VM_PT_SHARED))
		return addr;

	if (IS_ERR_VALUE(addr))
		return addr;

	/*
	 * Early invariants (MAP_FIXED, alignment, file-backed, writability)
	 * are checked in ptshare_validate_mmap()
	 */
	vma = vma_lookup(mm, addr);

	/* This VMA was just installed by do_mmap() and the write lock is still held */
	if (WARN_ON_ONCE(!vma))
		return -EINVAL;

	/*
	 * Enforce remaining invariants on the installed VMA
	 * (driver-controlled flags, file-backed, non-shmem, writability).
	 * Note: Early checks for !file and shmem_file(file) are also handled in
	 * ptshare_validate_mmap().
	 */
	if (!vma->vm_file || vma_is_anonymous(vma) ||
	    shmem_file(vma->vm_file) ||
	    (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP |
			      VM_DONTCOPY | VM_WRITE)))
		goto out_unmap;

	desc = mm->ptshare_desc;
	if (WARN_ON_ONCE(!desc))
		goto out_unmap;

	/* mm must refer to the private client MM */
	if (WARN_ON_ONCE(mm == desc->ptshare_mm))
		goto out_unmap;

	/* Initialize VMA's descriptor pointer */
	vma_set_ptshare_desc(vma, desc);

	install_err = __ptshare_install_vma(vma);
	if (!install_err)
		return addr;

	err = install_err;

out_unmap:
	vm_flags_clear(vma, VM_PT_SHARED);
	unmap_err = do_munmap(mm, addr, len, NULL);
	if (WARN_ON_ONCE(unmap_err)) {
		/*
		 * Rollback failed. We are in a bad state: the VMA is still
		 * mapped in the client MM but its VM_PT_SHARED flag has been
		 * cleared, meaning it is now a private mapping. Since we
		 * return the installation error to userspace, userspace will
		 * assume the mmap failed, leading to a resource leak and
		 * address space inconsistency.
		 *
		 * This is extremely unlikely to happen because:
		 * 1. The mmap write lock is held throughout the operation.
		 * 2. Merging of shared VMAs is disabled, so this VMA matches
		 *    the unmap range exactly. No splitting is required,
		 *    ruling out ENOMEM during do_munmap().
		 *
		 * We still return the original error (err) to propagate the
		 * root cause of the failure.
		 */
	}

	return err;
}

static inline void ptshare_remove_vma(struct ptshare_desc *desc,
				      unsigned long start, unsigned long end)
{
	struct mm_struct *ptshare_mm = desc->ptshare_mm;
	int err;

	/* Destroy the shadow VMA in ptshare_mm */
	mmap_write_lock(ptshare_mm);
	err = do_munmap(ptshare_mm, start, end - start, NULL);
	WARN_ON_ONCE(err);
	mmap_write_unlock(ptshare_mm);
}

int ptshare_get_vma(struct vm_area_struct *vma, struct ptshare_desc *desc)
{
	unsigned long addr = vma->vm_start;
	struct vm_area_struct *shadow_vma;

	if (WARN_ON_ONCE(!desc))
		return -EINVAL;

	mmap_read_lock(desc->ptshare_mm);
	shadow_vma = vma_lookup(desc->ptshare_mm, addr);
	if (WARN_ON_ONCE(!shadow_vma)) {
		mmap_read_unlock(desc->ptshare_mm);
		return -EINVAL;
	}
	refcount_inc(vma_ptshare_refcount(shadow_vma));
	mmap_read_unlock(desc->ptshare_mm);

	return 0;
}

/**
 * ptshare_put_vma - Release a reference to a shared VMA.
 * @vma: The client VMA.
 * @desc: The shared page table descriptor.
 *
 * This function implements a decoupled locking protocol to safely manage
 * the lifecycle of shared VMAs:
 *
 * 1. Exclusive Ownership: If the reference count drops to zero, the current
 *    thread acquires exclusive responsibility for destroying the shadow VMA.
 * 2. Decoupled Destruction: It is safe to perform the high-latency unmapping
 *    operations using the shadow MM's mmap_lock (see below).
 * 3. Collision Prevention: During the window between refcount drop and
 *    the final do_munmap(), any concurrent attempt to install an overlapping
 *    shared VMA will be rejected by ptshare_validate_mmap() because the
 *    VMA remains in the shadow MM tree until the unmap completes.
 *
 * Note: Since the refcount is atomic, we can potentially optimize this
 * by using per-VMA read locks for lookups and refcount manipulation.
 * This is left for a subsequent optimization.
 */
void ptshare_put_vma(struct vm_area_struct *vma, struct ptshare_desc *desc)
{
	unsigned long addr = vma->vm_start;
	struct vm_area_struct *shadow_vma;
	bool should_remove = false;
	unsigned long start, end;

	if (!desc)
		return;

	mmap_read_lock(desc->ptshare_mm);
	shadow_vma = vma_lookup(desc->ptshare_mm, addr);
	if (WARN_ON_ONCE(!shadow_vma)) {
		mmap_read_unlock(desc->ptshare_mm);
		return;
	}

	start = shadow_vma->vm_start;
	end = shadow_vma->vm_end;

	if (refcount_dec_and_test(vma_ptshare_refcount(shadow_vma)))
		should_remove = true;

	mmap_read_unlock(desc->ptshare_mm);

	if (should_remove)
		ptshare_remove_vma(desc, start, end);
}

/**
 * ptshare_exit - Clean up page table sharing state for an exiting mm.
 * @mm: The mm_struct being torn down.
 *
 * This function releases the process's reference to its sharing domain
 * descriptor (ptshare_desc). If this is a client MM, it decrements the
 * descriptor's reference count. If this is the shadow MM itself, it
 * triggers RCU-delayed freeing of the descriptor.
 *
 * This is called during __mmput() after exit_mmap() has detached the VMAs.
 */
void ptshare_exit(struct mm_struct *mm)
{
	struct ptshare_desc *desc = rcu_dereference_raw(mm->ptshare_desc);

	if (desc) {
		RCU_INIT_POINTER(mm->ptshare_desc, NULL);
		if (mm_is_ptshare_shadow(mm))
			ptshare_free_desc_rcu(desc);
		else
			ptshare_put_desc(desc);
	}
}

int ptshare_fork_vma(struct mm_struct *mm, struct vm_area_struct *vma)
{
	if (vma_shares_pagetables(vma)) {
		struct ptshare_desc *desc = vma_ptshare_desc(vma);
		int retval;

		retval = ptshare_get_vma(vma, desc);
		if (retval) {
			vm_flags_clear(vma, VM_PT_SHARED);
			return retval;
		}
		if (!rcu_access_pointer(mm->ptshare_desc)) {
			rcu_assign_pointer(mm->ptshare_desc, desc);
			ptshare_get_desc(desc);
		}
	}
	return 0;
}

int ptshare_unshare_vma_on_gup(struct mm_struct *mm, unsigned long addr,
			       int *locked, bool unlockable)
{
	struct vm_area_struct *vma;
	int ret;

	mmap_assert_locked(mm);

	/*
	 * If the caller does not allow us to drop the lock (e.g. they hold the
	 * write lock, or require read lock atomicity), we cannot safely acquire
	 * the write lock to perform unsharing. Fail the operation in this case.
	 */
	if (!unlockable)
		return -EFAULT;

	/* Drop the read lock and notify caller */
	mmap_read_unlock(mm);
	*locked = 0;

	/* Acquire write lock */
	ret = mmap_write_lock_killable(mm);
	if (ret) {
		mmap_read_lock(mm);
		*locked = 1;
		return ret;
	}

	/* Re-lookup VMA under write lock */
	vma = vma_lookup(mm, addr);
	if (vma && vma_shares_pagetables(vma)) {
		ret = ptshare_unshare_vma_locked(vma);
		if (ret) {
			mmap_write_unlock(mm);
			mmap_read_lock(mm);
			*locked = 1;
			return ret;
		}
	}

	/* Release write lock. We return with NO locks held. */
	mmap_write_unlock(mm);

	return 0;
}
