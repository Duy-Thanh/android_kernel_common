// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "../remap_pfn_cow_ppps.h"
#include "../../ppps/ppps_misc_module.h"

#define PROCESS_PAGE_SIZE	4096UL
#define TEST_SLICE		2U
#define TEST_MARKER		0x93

static void *test_buffer;

static int
remap_pfn_cow_ppps_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (vma->vm_end - vma->vm_start != PROCESS_PAGE_SIZE)
		return -EINVAL;
	return remap_pfn_range_slice(vma, vma->vm_start,
				     page_to_pfn(virt_to_page(test_buffer)),
				     TEST_SLICE, PROCESS_PAGE_SIZE,
				     vma->vm_page_prot);
}

static long remap_pfn_cow_ppps_ioctl(struct file *file, unsigned int cmd,
				     unsigned long arg)
{
	struct remap_pfn_cow_ppps_probe probe;
	struct vm_area_struct *vma;
	phys_addr_t expected;

	if (cmd != REMAP_PFN_COW_PPPS_IOCTL)
		return -ENOTTY;
	if (copy_from_user(&probe, (void __user *)arg, sizeof(probe)))
		return -EFAULT;

	mmap_read_lock(current->mm);
	vma = vma_lookup(current->mm, probe.address);
	if (!vma || vma->vm_file != file) {
		mmap_read_unlock(current->mm);
		return -ENOENT;
	}
	expected = virt_to_phys(test_buffer) +
		((phys_addr_t)TEST_SLICE << PAGE_SHIFT_COMPAT);
	probe.slice = vma_slice_off(vma);
	probe.offset_matches = vma_file_offset(vma) == expected;
	mmap_read_unlock(current->mm);

	if (copy_to_user((void __user *)arg, &probe, sizeof(probe)))
		return -EFAULT;
	return 0;
}

static const struct file_operations remap_pfn_cow_ppps_fops = {
	.owner = THIS_MODULE,
	.mmap = remap_pfn_cow_ppps_mmap,
	.unlocked_ioctl = remap_pfn_cow_ppps_ioctl,
	.compat_ioctl = remap_pfn_cow_ppps_ioctl,
};

static int remap_pfn_cow_ppps_setup(void)
{
	test_buffer = alloc_pages_exact(PAGE_SIZE, GFP_KERNEL | __GFP_ZERO);
	if (!test_buffer)
		return -ENOMEM;
	memset(test_buffer + TEST_SLICE * PROCESS_PAGE_SIZE, TEST_MARKER,
	       PROCESS_PAGE_SIZE);
	return 0;
}

static void remap_pfn_cow_ppps_teardown(void)
{
	free_pages_exact(test_buffer, PAGE_SIZE);
}

PPPS_MISC_MODULE("remap_pfn_cow_ppps", &remap_pfn_cow_ppps_fops, 0,
		 remap_pfn_cow_ppps_setup, remap_pfn_cow_ppps_teardown,
		 "PPPS private PFN remap offset regression test helper");
