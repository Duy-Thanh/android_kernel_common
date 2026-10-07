// SPDX-License-Identifier: GPL-2.0
/* Exercise the public remap wrapper using only this module's own storage. */
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/vmalloc.h>

#include "../../ppps/ppps_misc_module.h"

static unsigned char *buffer;

static int fixture_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long selector = vma_file_offset(vma);
	unsigned long offset;

	if (vma->vm_end - vma->vm_start > PAGE_SIZE)
		return -EINVAL;
	switch (selector) {
	case 0:
		offset = 0;
		break;
	case SZ_16K:
		offset = 1;
		break;
	case 2 * SZ_16K:
		offset = SZ_4K;
		break;
	case 3 * SZ_16K:
		offset = PAGE_SIZE;
		break;
	default:
		return -EINVAL;
	}
	vma->vm_pgoff = 0;
	vma_set_slice_off(vma, 0);
	return remap_vmalloc_range(vma, buffer + offset, 0);
}

static const struct file_operations fixture_fops = {
	.owner = THIS_MODULE,
	.mmap = fixture_mmap,
};

static int fixture_setup(void)
{
	unsigned int slice;

	if (PAGE_SIZE != SZ_16K)
		return -EOPNOTSUPP;
	buffer = vmalloc_user(2 * PAGE_SIZE);
	if (!buffer)
		return -ENOMEM;
	for (slice = 0; slice < PAGE_SIZE / SZ_4K; slice++) {
		memset(buffer + slice * SZ_4K, 0x41 + slice, SZ_4K);
		memset(buffer + PAGE_SIZE + slice * SZ_4K,
		       0x62 + slice, SZ_4K);
	}
	return 0;
}

static void fixture_teardown(void)
{
	vfree(buffer);
}

PPPS_MISC_MODULE("vmalloc_align_ppps", &fixture_fops, 0600,
		 fixture_setup, fixture_teardown,
		 "Native backing alignment for PPPS vmalloc remaps");
