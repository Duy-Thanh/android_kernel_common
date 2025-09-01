// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/sched/mm.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper void rust_helper_mmgrab(struct mm_struct *mm)
{
	mmgrab(mm);
}

__rust_helper void rust_helper_mmdrop(struct mm_struct *mm)
{
	mmdrop(mm);
}

__rust_helper void rust_helper_mmget(struct mm_struct *mm)
{
	mmget(mm);
}

__rust_helper bool rust_helper_mmget_not_zero(struct mm_struct *mm)
{
	return mmget_not_zero(mm);
}

__rust_helper void rust_helper_mmap_read_lock(struct mm_struct *mm)
{
	mmap_read_lock(mm);
}

__rust_helper bool rust_helper_mmap_read_trylock(struct mm_struct *mm)
{
	return mmap_read_trylock(mm);
}

__rust_helper void rust_helper_mmap_read_unlock(struct mm_struct *mm)
{
	mmap_read_unlock(mm);
}

__rust_helper struct vm_area_struct *rust_helper_vma_lookup(struct mm_struct *mm,
					      unsigned long addr)
{
	return vma_lookup(mm, addr);
}

__rust_helper void rust_helper_vma_end_read(struct vm_area_struct *vma)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
void rust_helper_mmgrab(struct mm_struct *mm)
{
	mmgrab(mm);
}

void rust_helper_mmdrop(struct mm_struct *mm)
{
	mmdrop(mm);
}

void rust_helper_mmget(struct mm_struct *mm)
{
	mmget(mm);
}

bool rust_helper_mmget_not_zero(struct mm_struct *mm)
{
	return mmget_not_zero(mm);
}

void rust_helper_mmap_read_lock(struct mm_struct *mm)
{
	mmap_read_lock(mm);
}

bool rust_helper_mmap_read_trylock(struct mm_struct *mm)
{
	return mmap_read_trylock(mm);
}

void rust_helper_mmap_read_unlock(struct mm_struct *mm)
{
	mmap_read_unlock(mm);
}

struct vm_area_struct *rust_helper_vma_lookup(struct mm_struct *mm,
					      unsigned long addr)
{
	return vma_lookup(mm, addr);
}

void rust_helper_vma_end_read(struct vm_area_struct *vma)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	vma_end_read(vma);
}
