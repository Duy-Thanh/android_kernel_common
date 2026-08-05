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

int ptshare_unshare_vma_locked(struct vm_area_struct *vma)
{
	mmap_assert_write_locked(vma->vm_mm);
	if (vma_shares_pagetables(vma))
		return -EINVAL;
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

	vma = vma_lookup(mm, addr);

	/* This VMA was just installed by do_mmap() and the write lock is still held */
	if (WARN_ON_ONCE(!vma))
		return -EINVAL;

	/* Enforce remaining invariants on the installed VMA */
	if (!vma->vm_file || vma_is_anonymous(vma) ||
	    (vma->vm_flags & (VM_PFNMAP | VM_MIXEDMAP | VM_DONTCOPY)))
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

void ptshare_get_vma(struct vm_area_struct *vma, struct ptshare_desc *desc)
{
	unsigned long addr = vma->vm_start;
	struct vm_area_struct *shadow_vma;

	if (WARN_ON_ONCE(!desc))
		return;

	mmap_read_lock(desc->ptshare_mm);
	shadow_vma = vma_lookup(desc->ptshare_mm, addr);
	if (WARN_ON_ONCE(!shadow_vma)) {
		mmap_read_unlock(desc->ptshare_mm);
		return;
	}
	refcount_inc(vma_ptshare_refcount(shadow_vma));
	mmap_read_unlock(desc->ptshare_mm);
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
