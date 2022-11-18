/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/mm.h>
#include <linux/module.h>

#include <asm/tlbflush.h>

void fix_range_common(struct mm_struct *mm, unsigned long start_addr,
		      unsigned long end_addr, int force)
{
	panic("%s: not implemented\n", __func__);
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long address)
{
	panic("%s: not implemented\n", __func__);
}

void flush_tlb_all(void)
{
	panic("%s: not implemented\n", __func__);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	panic("%s: not implemented\n", __func__);
}

void flush_tlb_kernel_vm(void)
{
	panic("%s: not implemented\n", __func__);
}

void __flush_tlb_one(unsigned long addr)
{
	panic("%s: not implemented\n", __func__);
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	// nothing to do as we don't emulate HW virtual memory support
}
EXPORT_SYMBOL(flush_tlb_range);

void flush_tlb_mm_range(struct mm_struct *mm, unsigned long start,
			unsigned long end)
{
	panic("%s: not implemented\n", __func__);
}

void flush_tlb_mm(struct mm_struct *mm)
{
	panic("%s: not implemented\n", __func__);
}

void force_flush_all(void)
{
	panic("%s: not implemented\n", __func__);
}
