// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/module.h>

#include "../../ppps/ppps_misc_module.h"

#define DEVICE_NAME "mremap_pgoff_ppps"
#define USER_PAGE_SIZE 4096UL

static const struct vm_operations_struct mremap_pgoff_ppps_vm_ops;

static unsigned long mremap_get_area(struct file *file, unsigned long addr,
				     unsigned long len, unsigned long pgoff,
				     unsigned long flags)
{
	if (pgoff != 1)
		return -EINVAL;
	return mm_get_unmapped_area(current->mm, file, addr, len, pgoff, flags);
}

static int mremap_pgoff_ppps_mmap(struct file *file,
				  struct vm_area_struct *vma)
{
	if (vma->vm_end - vma->vm_start != USER_PAGE_SIZE)
		return -EINVAL;
	vma->vm_ops = &mremap_pgoff_ppps_vm_ops;
	vm_flags_set(vma, VM_DONTDUMP);
	return 0;
}

static const struct file_operations mremap_pgoff_ppps_fops = {
	.owner = THIS_MODULE,
	.mmap = mremap_pgoff_ppps_mmap,
	.get_unmapped_area = mremap_get_area,
};

PPPS_MISC_MODULE(DEVICE_NAME, &mremap_pgoff_ppps_fops, 0,
		 ppps_misc_no_setup, ppps_misc_no_teardown,
		 "PPPS mremap file-offset regression helper");
