// SPDX-License-Identifier: GPL-2.0-or-later

#define KMSG_COMPONENT "zram_ioctl"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/sched/task.h>
#include <linux/pagewalk.h>
#include <linux/swapops.h>

#include "zram_drv.h"
#include "zram_ioctl.h"

#if IS_ENABLED(CONFIG_ZRAM_WRITEBACK)

/* Private data for the page table walker. */
struct zram_process_walk_private {
	struct zram *zram;
	struct zram_pp_ctl *pp_ctl;
};

static inline bool can_do_file_pageout(struct vm_area_struct *vma)
{
        if (!vma->vm_file)
                return false;
        /*
         * paging out pagecache only for non-anonymous mappings that correspond
         * to the files the calling process could (if tried) open for writing;
         * otherwise we'd be including shared non-exclusive mappings, which
         * opens a side channel.
         */
        return inode_owner_or_capable(&nop_mnt_idmap,
                                      file_inode(vma->vm_file)) ||
               file_permission(vma->vm_file, MAY_WRITE) == 0;
}

/*
 * pmd_entry callback for walk_page_range().
 *
 * This function is called for each PMD in a VMA. It checks if the PTE
 * corresponds to a swapped-out page.
 */
static int zram_process_walker(pmd_t *pmd, unsigned long start,
			       unsigned long end, struct mm_walk *walk)
{
	struct zram_process_walk_private *private = walk->private;
	struct zram *zram = private->zram;
	struct zram_pp_ctl *pp_ctl = private->pp_ctl;
	struct vm_area_struct *vma = walk->vma;
	pte_t *ptep, pte;
	swp_entry_t entry;
	spinlock_t *ptl;
	unsigned long addr;
	u32 index;

	for (addr = start; addr < end; addr += PAGE_SIZE) {
		ptep = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
		if (!ptep)
			break;

		pte = ptep_get(ptep);
		pte_unmap_unlock(ptep, ptl);

		if (!is_swap_pte(pte))
			continue;
		entry = pte_to_swp_entry(pte);
		if (unlikely(non_swap_entry(entry)))
			continue;

		index = swp_offset(entry);

		/* Use PAGE_WRITEBACK for single index */
		scan_slots_for_writeback(zram, 0, index, index+1, pp_ctl);
	}

	cond_resched();
	return 0;
}

static const struct mm_walk_ops zram_walk_ops = {
	.pmd_entry = zram_process_walker,
	.walk_lock = PGWALK_RDLOCK,
};

int zram_ioctl_process_writeback_scan(struct zram *zram,
				      struct zram_ioc_data *ioc_data,
				      struct zram_pp_ctl *ctl)
{
	struct mm_struct *mm;
	struct vm_area_struct *vma;
	struct task_struct *task;
	unsigned int f_flags;
	int ret = 0;

	struct zram_process_walk_private private = {
		.zram = zram,
		.pp_ctl = ctl
	};

	task = pidfd_get_task(ioc_data->data.process_writeback.pidfd, &f_flags);
	if (IS_ERR(task)) {
		return PTR_ERR(task);
	}

	/* Require PTRACE_MODE_READ to avoid leaking ASLR metadata. */
	mm = mm_access(task, PTRACE_MODE_READ_FSCREDS);
	if (IS_ERR_OR_NULL(mm)) {
		ret = IS_ERR(mm) ? PTR_ERR(mm) : -ESRCH;
		goto release_task;
	}

	/* Require CAP_SYS_NICE for influencing process performance. */
	if (!capable(CAP_SYS_NICE)) {
		ret = -EPERM;
		goto release_mm;
	}

	VMA_ITERATOR(vmi, mm, 0);
	/* Iterates through all the VMAs of the process */
	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		if (!vma_is_anonymous(vma) && (!can_do_file_pageout(vma) &&
					       (vma->vm_flags & VM_MAYSHARE)))
			continue;

		ret = walk_page_range(mm, vma->vm_start, vma->vm_end,
				      &zram_walk_ops, &private);
		if (ret)
			break;
	}
	mmap_read_unlock(mm);

release_mm:
	mmput(mm);
release_task:
	put_task_struct(task);

	return ret;
}
#endif
