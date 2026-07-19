// SPDX-License-Identifier: GPL-2.0
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/mmap_lock.h>
#include <linux/module.h>
#include <linux/mmu_context.h>
#include <linux/hugetlb.h>
#include <linux/pgalloc.h>

#include <asm/tlb.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#include "internal.h"

bool reclaim_pt_is_enabled(unsigned long start, unsigned long end,
			   struct zap_details *details)
{
	return details && (details->zap_flags & ZAP_FLAG_RECLAIM_PT) && (end - start >= PMD_SIZE);
}

bool try_get_and_clear_pmd(struct mm_struct *mm, pmd_t *pmd, pmd_t *pmdval)
{
	spinlock_t *pml = pmd_lockptr(mm, pmd);

	if (!spin_trylock(pml))
		return false;

	*pmdval = pmdp_get_lockless(pmd);
	pmd_clear(pmd);
	spin_unlock(pml);

	return true;
}

void free_pte(struct mm_struct *mm, unsigned long addr, struct mmu_gather *tlb,
	      pmd_t pmdval)
{
	pte_free_tlb(tlb, pmd_pgtable(pmdval), addr);
	mm_dec_nr_ptes(mm);
}

int try_to_free_pte(struct mm_struct *mm, pmd_t *pmd, unsigned long addr,
		     struct mmu_gather *tlb)
{
	pmd_t pmdval;
	spinlock_t *pml, *ptl = NULL;
	pte_t *start_pte, *pte;
	int i;

	pml = pmd_lock(mm, pmd);
	start_pte = pte_offset_map_rw_nolock(mm, pmd, addr, &pmdval, &ptl);
	if (!start_pte)
		goto out_ptl;
	if (ptl != pml)
		spin_lock_nested(ptl, SINGLE_DEPTH_NESTING);

	/* Check if it is empty PTE page */
	for (i = 0, pte = start_pte; i < PTRS_PER_PTE; i++, pte++) {
		if (!pte_none(ptep_get(pte)))
			goto out_ptl;
	}
	pte_unmap(start_pte);

	pmd_clear(pmd);

	if (ptl != pml)
		spin_unlock(ptl);
	spin_unlock(pml);

	free_pte(mm, addr, tlb, pmdval);

	return 1;
out_ptl:
	if (start_pte)
		pte_unmap_unlock(start_pte, ptl);
	if (ptl != pml)
		spin_unlock(pml);
	return 0;
}

static int last_scanned_pid = 1;

int scan_and_free_empty_ptes(int max_processes)
{
	struct task_struct *task;
	struct mm_struct *mm;
	int scanned = 0;
	int freed_ptes = 0;
	unsigned long addr;
	pgd_t *pgd; p4d_t *p4d; pud_t *pud; pmd_t *pmd; pte_t *pte_base;
	int i, valid_count;
	struct mmu_gather tlb;

	rcu_read_lock();
	for_each_process(task) {
		if (task->pid < last_scanned_pid)
			continue;

		last_scanned_pid = task->pid + 1;

		get_task_struct(task);
		rcu_read_unlock();

		mm = get_task_mm(task);
		if (mm) {
			int task_freed = 0;
			tlb_gather_mmu(&tlb, mm);

			/* Use killable lock to prevent hanging if task is OOM killed */
			if (mmap_read_lock_killable(mm)) {
				tlb_finish_mmu(&tlb);
				mmput(mm);
				continue;
			}

			for (addr = 0; addr < TASK_SIZE; ) {
				/* Avoid hogging CPU for huge address spaces */
				cond_resched();

				pgd = pgd_offset(mm, addr);
				if (pgd_none(*pgd) || pgd_bad(*pgd)) {
					addr = (addr + PGDIR_SIZE) & PGDIR_MASK;
					continue;
				}

				p4d = p4d_offset(pgd, addr);
				if (p4d_none(*p4d) || p4d_bad(*p4d)) {
					addr = (addr + P4D_SIZE) & P4D_MASK;
					continue;
				}

				pud = pud_offset(p4d, addr);
				if (pud_none(*pud) || pud_bad(*pud)) {
					addr = (addr + PUD_SIZE) & PUD_MASK;
					continue;
				}

				pmd = pmd_offset(pud, addr);
				if (pmd_none(*pmd) || pmd_bad(*pmd) || pmd_trans_huge(*pmd)) {
					addr = (addr + PMD_SIZE) & PMD_MASK;
					continue;
				}

				pte_base = pte_offset_map(pmd, addr);
				if (!pte_base) {
					addr += PAGE_SIZE;
					continue;
				}

				valid_count = 0;
				for (i = 0; i < PTRS_PER_PTE; i++) {
					if (!pte_none(ptep_get(&pte_base[i]))) {
						valid_count++;
						break;
					}
				}
				pte_unmap(pte_base);

				if (valid_count == 0) {
					int freed = try_to_free_pte(mm, pmd, addr, &tlb);
					freed_ptes += freed;
					task_freed += freed;
				}

				addr = (addr + PMD_SIZE) & PMD_MASK;
			}

			mmap_read_unlock(mm);
			tlb_finish_mmu(&tlb);
			mmput(mm);

			if (task_freed > 0) {
				pr_info("pixelmd: PID %d (%s) freed %d empty PTE pages\n",
					task->pid, task->comm, task_freed);
			}
		}

		put_task_struct(task);
		scanned++;

		if (scanned >= max_processes)
			return freed_ptes;

		rcu_read_lock();
	}
	rcu_read_unlock();

	/* Wrap around for the next cycle */
	last_scanned_pid = 1;

	return freed_ptes;
}
EXPORT_SYMBOL_GPL(scan_and_free_empty_ptes);

static ssize_t trigger_store(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	int freed;
	/* Reset to sweep from the beginning */
	last_scanned_pid = 1;
	freed = scan_and_free_empty_ptes(1000000); /* arbitrarily large to scan all processes */
	pr_info("pte_reclaim: triggered via sysfs, freed %d empty PTE pages\n", freed);
	return count;
}

static struct kobj_attribute trigger_attribute = __ATTR_WO(trigger);

static struct attribute *pte_reclaim_attrs[] = {
	&trigger_attribute.attr,
	NULL,
};

static struct attribute_group pte_reclaim_attr_group = {
	.attrs = pte_reclaim_attrs,
};

static struct kobject *pte_reclaim_kobj;

static int __init pte_reclaim_sysfs_init(void)
{
	int err;

	pte_reclaim_kobj = kobject_create_and_add("pte_reclaim", mm_kobj);
	if (!pte_reclaim_kobj)
		return -ENOMEM;

	err = sysfs_create_group(pte_reclaim_kobj, &pte_reclaim_attr_group);
	if (err)
		kobject_put(pte_reclaim_kobj);

	return err;
}
subsys_initcall(pte_reclaim_sysfs_init);
