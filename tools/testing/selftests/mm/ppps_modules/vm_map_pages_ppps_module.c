// SPDX-License-Identifier: GPL-2.0

#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/ppps.h>

#include "../vm_map_pages_ppps.h"
#include "../../ppps/ppps_misc_module.h"

#define USER_PAGE_SIZE	4096UL
#define TEST_PAGE_COUNT	8

static struct page *test_pages[TEST_PAGE_COUNT];

static int expected_error(int error, int expected)
{
	if (error == expected)
		return 0;
	return error ?: -EINVAL;
}

static int vm_insert_pages_edge_case(struct vm_area_struct *vma)
{
	unsigned long offset = (vma->vm_pgoff << PPPS_SLICE_SHIFT) +
		vma_slice_off(vma);
	unsigned long count = 1;
	int error;

	switch (offset) {
	case VM_MAP_PAGES_PPPS_ZERO:
		count = 0;
		return vm_insert_pages(vma, vma->vm_start, test_pages, &count);
	case VM_MAP_PAGES_PPPS_BEFORE:
		error = vm_insert_pages(vma, vma->vm_start - USER_PAGE_SIZE,
					test_pages, &count);
		return expected_error(error, -EFAULT);
	case VM_MAP_PAGES_PPPS_AFTER:
		error = vm_insert_pages(vma, vma->vm_end, test_pages, &count);
		return expected_error(error, -EFAULT);
	case VM_MAP_PAGES_PPPS_TOO_MANY:
		count = 2;
		error = vm_insert_pages(vma, vma->vm_start, test_pages, &count);
		return expected_error(error, -EFAULT);
	case VM_MAP_PAGES_PPPS_BUSY:
		error = vm_insert_pages(vma, vma->vm_start, test_pages, &count);
		if (error || count)
			return error ?: -EINVAL;
		count = 1;
		error = vm_insert_pages(vma, vma->vm_start, test_pages, &count);
		if (count != 1)
			return -EINVAL;
		return expected_error(error, -EBUSY);
	case VM_MAP_PAGES_PPPS_NATIVE_PARTIAL:
		error = vm_insert_page_slice(vma,
					     vma->vm_start + USER_PAGE_SIZE,
					     test_pages[1], 0);
		if (error)
			return error;
		error = vm_insert_page_native(vma, vma->vm_start,
					      test_pages[0]);
		return expected_error(error, -EBUSY);
	default:
		return -EINVAL;
	}
}

static int vm_map_pages_ppps_mmap(struct file *file,
				  struct vm_area_struct *vma)
{
	unsigned long offset = (vma->vm_pgoff << PPPS_SLICE_SHIFT) +
		vma_slice_off(vma);

	if (offset >= VM_MAP_PAGES_PPPS_ZERO)
		return vm_insert_pages_edge_case(vma);
	return vm_map_pages(vma, test_pages, ARRAY_SIZE(test_pages));
}

static const struct file_operations vm_map_pages_ppps_fops = {
	.owner = THIS_MODULE,
	.mmap = vm_map_pages_ppps_mmap,
};

static void free_test_pages(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(test_pages); i++) {
		if (test_pages[i])
			__free_page(test_pages[i]);
	}
}

static int allocate_test_pages(void)
{
	unsigned int logical_page = 0;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(test_pages); i++) {
		unsigned char *address;
		unsigned int offset;

		test_pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!test_pages[i])
			return -ENOMEM;
		address = kmap_local_page(test_pages[i]);
		for (offset = 0; offset < PAGE_SIZE; offset += USER_PAGE_SIZE)
			memset(address + offset, 0x40 + logical_page++,
			       USER_PAGE_SIZE);
		kunmap_local(address);
	}
	return 0;
}

static int vm_map_pages_ppps_setup(void)
{
	int error = allocate_test_pages();

	if (error)
		free_test_pages();
	return error;
}

PPPS_MISC_MODULE("vm_map_pages_ppps", &vm_map_pages_ppps_fops, 0,
		 vm_map_pages_ppps_setup, free_test_pages,
		 "PPPS vm_map_pages regression test helper");
