/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_PROC_H
#define _LINUX_P3S_PROC_H

#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/kernel-page-flags.h>
#include <linux/sched.h>

/**
 * mm_pinned_vm_pages - pinned_vm of @mm in pages of mm_pte_size(@mm)
 * @mm: mm_struct to report
 *
 * pinned_vm counts kernel pages, e.g. from perf_mmap(), while task_mem()
 * prints the other counters of @mm in its own page size. Keep the 64-bit
 * counter width for native configurations with a 32-bit unsigned long.
 *
 * Return: Pinned memory expressed in pages of mm_pte_size(@mm) bytes.
 */
static inline u64 mm_pinned_vm_pages(const struct mm_struct *mm)
{
	return (u64)atomic64_read(&mm->pinned_vm) * mm_slices_per_page(mm);
}

/* Preserve architecture-specific MMU page sizes outside P3S VMAs. */
static inline unsigned long p3s_vma_mmu_pagesize(struct vm_area_struct *vma)
{
	if (vma_is_p3s_4k(vma))
		return vma_page_size(vma);
	return vma_mmu_pagesize(vma);
}

/*
 * vma_slice_ptes - Count the PTEs around @ptep that map @page
 * @vma: VMA that maps @addr
 * @ptep: PTE that maps @addr to @page
 * @addr: Address that @ptep maps
 * @page: Page that @ptep maps
 *
 * A native VMA maps a page with one PTE, so this returns 1. A 4KB VMA maps
 * slice i of the 16KB page that backs @addr, if at all, i - s 4KB pages
 * from @addr, where s is vma_slice_offset(@vma, @addr). Count the present
 * PTEs there, @ptep included, that map @page, without leaving @ptep's page
 * table. Each of them adds one to the mapcount of @page, so only this mm
 * maps @page if its mapcount is no larger than the count.
 */
static inline int vma_slice_ptes(struct vm_area_struct *vma, pte_t *ptep,
				 unsigned long addr, struct page *page)
{
	unsigned int s, i;
	int nr = 0;

	if (!vma_is_p3s_4k(vma))
		return 1;

	s = vma_slice_offset(vma, addr);
	for (i = 0; i < P3S_SLICES_PER_PAGE; i++) {
		long d = (long)i - (long)s;
		unsigned long a = addr + d * (long)PAGE_SIZE_4KB;
		pte_t pte;

		if ((a ^ addr) & ~(PMD_SIZE_4KB - 1))
			continue;
		pte = ptep_get(ptep + d);
		if (pte_present(pte) && pte_page(pte) == page)
			nr++;
	}
	return nr;
}

/*
 * /proc/kpageflags, /proc/kpagecount and /proc/kpagecgroup are indexed by
 * the PFNs of /proc/<pid>/pagemap, which counts them in the page size of
 * the mm, so a 4KB process gets 4KB frame numbers. Index the kpage files
 * in frames of the reader's page size: each kernel page then appears once
 * per slice, and native readers see no change.
 */

/* Number of kpage entries that cover @nr kernel pages */
static inline unsigned long kpage_reader_pfns(unsigned long nr)
{
	return nr * mm_slices_per_page(current->mm);
}

/* Kernel PFN of the kpage entry @pfn */
static inline unsigned long kpage_kernel_pfn(unsigned long pfn)
{
	return pfn >> (PAGE_SHIFT_KERNEL - mm_pte_shift(current->mm));
}

/*
 * kpage_reader_flags - Flags of the kpage entry @pfn
 * @pfn: kpage entry, in frames of the reader's page size
 * @flags: stable_page_flags() of the kernel page of @pfn
 *
 * /proc/kpageflags marks a compound page with one KPF_COMPOUND_HEAD entry,
 * followed by KPF_COMPOUND_TAIL entries. A 4KB reader sees the head page
 * of a compound page once per slice, so report only slice 0 as the head
 * and the other slices as tails.
 */
static inline u64 kpage_reader_flags(unsigned long pfn, u64 flags)
{
	if ((flags & BIT_ULL(KPF_COMPOUND_HEAD)) &&
	    (pfn & (mm_slices_per_page(current->mm) - 1))) {
		flags &= ~BIT_ULL(KPF_COMPOUND_HEAD);
		flags |= BIT_ULL(KPF_COMPOUND_TAIL);
	}
	return flags;
}

/*
 * kpage_reader_mapcount - Mapcount of a kernel page per page of the reader
 * @mapcount: Mapcount of the kernel page
 *
 * Each 4KB PTE that maps a slice of a kernel page adds one to its
 * mapcount, so a page that N 4KB processes map in full has a mapcount of
 * N * 4. Divide by the slices per reader page, rounding up so that a
 * mapped page never reads as unmapped. The reader's page size divided by
 * the result is then, up to rounding, the share of the kernel page that
 * smaps_account() gives each PTE.
 */
static inline u64 kpage_reader_mapcount(u64 mapcount)
{
	return DIV_ROUND_UP_ULL(mapcount, mm_slices_per_page(current->mm));
}

#endif /* _LINUX_P3S_PROC_H */
