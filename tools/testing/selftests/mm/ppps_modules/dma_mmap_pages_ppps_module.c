// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/module.h>

#include "../../ppps/ppps_misc_module.h"

#define USER_PAGE_SIZE	4096UL
#define TEST_BUFFER_SIZE	(4 * USER_PAGE_SIZE)

static struct page *test_page;

static int dma_mmap_pages_ppps_mmap(struct file *file,
				    struct vm_area_struct *vma)
{
	return dma_mmap_pages(NULL, vma, TEST_BUFFER_SIZE, test_page);
}

static const struct file_operations dma_mmap_pages_ppps_fops = {
	.owner = THIS_MODULE,
	.mmap = dma_mmap_pages_ppps_mmap,
};

static int dma_mmap_pages_ppps_setup(void)
{
	unsigned int offset;

	test_page = alloc_pages(GFP_KERNEL | __GFP_ZERO,
				get_order(TEST_BUFFER_SIZE));
	if (!test_page)
		return -ENOMEM;

	for (offset = 0; offset < TEST_BUFFER_SIZE; offset += PAGE_SIZE) {
		unsigned char *address;
		unsigned int page_offset;

		address = kmap_local_page(test_page + offset / PAGE_SIZE);
		for (page_offset = 0; page_offset < PAGE_SIZE;
		     page_offset += USER_PAGE_SIZE)
			memset(address + page_offset,
			       0x51 + (offset + page_offset) / USER_PAGE_SIZE,
			       USER_PAGE_SIZE);
		kunmap_local(address);
	}
	return 0;
}

static void dma_mmap_pages_ppps_teardown(void)
{
	__free_pages(test_page, get_order(TEST_BUFFER_SIZE));
}

PPPS_MISC_MODULE("dma_mmap_pages_ppps", &dma_mmap_pages_ppps_fops, 0,
		 dma_mmap_pages_ppps_setup, dma_mmap_pages_ppps_teardown,
		 "PPPS dma_mmap_pages regression test helper");
