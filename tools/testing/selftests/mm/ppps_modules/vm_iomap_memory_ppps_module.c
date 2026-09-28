// SPDX-License-Identifier: GPL-2.0

#include <linux/mm.h>
#include <linux/module.h>

#include "../../ppps/ppps_misc_module.h"

#define USER_PAGE_SIZE	4096UL
#define TEST_BUFFER_SIZE	(4 * USER_PAGE_SIZE)

static void *test_buffer;

static int vm_iomap_memory_ppps_mmap(struct file *file,
				     struct vm_area_struct *vma)
{
	return vm_iomap_memory(vma, virt_to_phys(test_buffer), TEST_BUFFER_SIZE);
}

static const struct file_operations vm_iomap_memory_ppps_fops = {
	.owner = THIS_MODULE,
	.mmap = vm_iomap_memory_ppps_mmap,
};

static int vm_iomap_memory_ppps_setup(void)
{
	unsigned int offset;

	test_buffer = alloc_pages_exact(TEST_BUFFER_SIZE, GFP_KERNEL | __GFP_ZERO);
	if (!test_buffer)
		return -ENOMEM;
	for (offset = 0; offset < TEST_BUFFER_SIZE; offset += USER_PAGE_SIZE)
		memset(test_buffer + offset, 0x71 + offset / USER_PAGE_SIZE,
		       USER_PAGE_SIZE);
	return 0;
}

static void vm_iomap_memory_ppps_teardown(void)
{
	free_pages_exact(test_buffer, TEST_BUFFER_SIZE);
}

PPPS_MISC_MODULE("vm_iomap_memory_ppps", &vm_iomap_memory_ppps_fops, 0,
		 vm_iomap_memory_ppps_setup, vm_iomap_memory_ppps_teardown,
		 "PPPS vm_iomap_memory regression test helper");
