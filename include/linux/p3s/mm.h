/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_P3S_MM_H
#define _LINUX_P3S_MM_H

#include <linux/kernel.h>
#include <linux/personality.h>
#include <linux/p3s/const.h>

struct mm_struct;
struct task_struct;

#ifdef CONFIG_ARM64_PER_PROCESS_PAGE_SIZE

/*
 * Boot-time command-line 4KB mode (p3s=):
 * P3S_4K_MODE_OFF:       Standard behavior (controlled via personality)
 * P3S_4K_MODE_ON:        Force 4KB pages for all processes after boot
 * P3S_4K_MODE_ALTERNATE: Alternate 4KB (odd PIDs) and 16KB (even PIDs)
 *                        for stress testing
 */
enum p3s_4k_mode {
	P3S_4K_MODE_OFF = 0,
	P3S_4K_MODE_ON = 1,
	P3S_4K_MODE_ALTERNATE = 2,
};

extern enum p3s_4k_mode p3s_4k_mode;

static inline bool personality_4kb_pages(unsigned long pers)
{
	return !!(pers & ADDR_PAGE_SIZE_4KB);
}

static inline unsigned int mm_pte_shift(const struct mm_struct *mm)
{
	return (mm && mm->context.pte_shift) ? mm->context.pte_shift :
					       PAGE_SHIFT_KERNEL;
}

static inline void mm_set_pte_shift(struct mm_struct *mm, unsigned int shift)
{
	if (mm)
		mm->context.pte_shift = shift;
}

static inline void mm_init_pte_shift(struct mm_struct *mm,
				     const struct task_struct *p)
{
	const struct task_struct *tsk = p ? p : current;
	bool use_4kb = false;

	/*
	 * In ALTERNATE mode, assign 4KB to odd PIDs and 16KB to even PIDs
	 * based strictly on PID parity rather than inheriting parent pte_shift,
	 * ensuring child tasks alternate regardless of what init or parent ran.
	 */
	if (system_state >= SYSTEM_RUNNING) {
		if (tsk && personality_4kb_pages(tsk->personality))
			use_4kb = true;
		else if (p3s_4k_mode == P3S_4K_MODE_ON)
			use_4kb = true;
		else if (p3s_4k_mode == P3S_4K_MODE_ALTERNATE &&
			 tsk && (tsk->pid & 1))
			use_4kb = true;
	}

	mm_set_pte_shift(mm, use_4kb ? PAGE_SHIFT_4KB : PAGE_SHIFT_KERNEL);
}

#else /* !CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

static inline bool personality_4kb_pages(unsigned long pers)
{
	return false;
}

static inline unsigned int mm_pte_shift(const struct mm_struct *mm)
{
	return PAGE_SHIFT_KERNEL;
}

static inline void mm_set_pte_shift(struct mm_struct *mm, unsigned int shift)
{
}

static inline void mm_init_pte_shift(struct mm_struct *mm,
				     const struct task_struct *p)
{
}

#endif /* CONFIG_ARM64_PER_PROCESS_PAGE_SIZE */

static inline unsigned long mm_pte_size(const struct mm_struct *mm)
{
	return _AC(1, UL) << mm_pte_shift(mm);
}

static inline unsigned long mm_pte_mask(const struct mm_struct *mm)
{
	return ~(mm_pte_size(mm) - 1);
}

/*
 * Check if the address space is operating in 4KB backward-compatibility mode
 * on the 16KB host kernel (i.e. mm_pte_shift(mm) != PAGE_SHIFT_KERNEL).
 */
static inline bool mm_is_p3s_4k(const struct mm_struct *mm)
{
	return mm_pte_shift(mm) != PAGE_SHIFT_KERNEL;
}

static inline unsigned long mm_pmd_size(const struct mm_struct *mm)
{
	return mm_is_p3s_4k(mm) ? PMD_SIZE_4KB : PMD_SIZE;
}

static inline unsigned long mm_pgdir_size(const struct mm_struct *mm)
{
	return mm_is_p3s_4k(mm) ? PGDIR_SIZE_4KB : PGDIR_SIZE;
}

static inline unsigned int mm_slices_per_page(const struct mm_struct *mm)
{
	return 1U << (PAGE_SHIFT_KERNEL - mm_pte_shift(mm));
}

static inline unsigned long mm_offset_in_page(const struct mm_struct *mm,
					      unsigned long addr)
{
	return addr & (mm_pte_size(mm) - 1);
}

static inline bool mm_pte_aligned(const struct mm_struct *mm,
				  unsigned long addr)
{
	return !mm_offset_in_page(mm, addr);
}

static inline unsigned long mm_pages_to_kb(const struct mm_struct *mm,
					   unsigned long nr)
{
	return nr << (mm_pte_shift(mm) - 10);
}

static inline unsigned long mm_phys_pfn(const struct mm_struct *mm,
					phys_addr_t x)
{
	return (unsigned long)(x >> mm_pte_shift(mm));
}

extern unsigned long stack_guard_gap;

static inline unsigned long mm_stack_guard_gap(const struct mm_struct *mm)
{
	return (stack_guard_gap >> PAGE_SHIFT_KERNEL) << mm_pte_shift(mm);
}

/*
 * mm_mmap_pgoff - Convert userspace process pgoff to host folio pgoff
 * @mm: Pointer to struct mm_struct
 * @pgoff: Page offset passed to mmap in process page units
 *
 * For 4KB compat processes on a 16KB kernel, shifts out the subpage slice
 * bits (pgoff >> 2) to obtain the enclosing 16KB Page Cache folio index:
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ Backing File (4KB Process Page Offsets)                 │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   pgoff 4   │   pgoff 5   │   pgoff 6   │    pgoff 7    │
 * │  (Slice 0)  │  (Slice 1)  │  (Slice 2)  │   (Slice 3)   │
 * ├─────────────┴─────────────┴─────────────┴───────────────┤
 * │ Enclosing Host 16KB Folio (vm_pgoff = 4 >> 2 = 1)       │
 * └─────────────────────────────────────────────────────────┘
 *               ▲
 *               └── mmap(pgoff = 5) -> mm_mmap_pgoff = 1
 */
static inline pgoff_t mm_mmap_pgoff(const struct mm_struct *mm, pgoff_t pgoff)
{
	return mm_is_p3s_4k(mm) ? (pgoff >> P3S_SLICE_SHIFT) : pgoff;
}

/*
 * mm_mmap_slice_off - Extract subpage slice offset from userspace pgoff
 * @mm: Pointer to struct mm_struct
 * @pgoff: Page offset passed to mmap in process page units
 *
 * For 4KB compat processes on a 16KB kernel, extracts the starting 4KB
 * subpage slice index (0..3) within the host 16KB folio:
 *
 * ┌─────────────────────────────────────────────────────────┐
 * │ 16KB Host Folio (Physical Page)                         │
 * ├─────────────┬─────────────┬─────────────┬───────────────┤
 * │   Slice 0   │   Slice 1   │   Slice 2   │    Slice 3    │
 * └─────────────┴─────────────┴─────────────┴───────────────┘
 *               ▲
 *               └── mmap(pgoff = 5) -> mm_mmap_slice_off = 1
 */
static inline unsigned short mm_mmap_slice_off(const struct mm_struct *mm,
					       pgoff_t pgoff)
{
	return mm_is_p3s_4k(mm) ? (pgoff & P3S_SLICE_MASK) : 0;
}

#endif /* _LINUX_P3S_MM_H */
