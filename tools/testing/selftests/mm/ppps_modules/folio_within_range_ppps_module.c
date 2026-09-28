// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/sizes.h>
#include <linux/string.h>

#include "../folio_within_range_ppps.h"
#include "../../../../../mm/internal.h"

#include "../../ppps/ppps_misc_module.h"

#define DEVICE_NAME "folio_within_range_ppps"
#define SUBPAGE_OFFSET SZ_4K

static const struct vm_operations_struct fixture_vm_ops;

static void fixture_init_vma(struct vm_area_struct *vma,
			     unsigned long file_offset, unsigned long length)
{
	memset(vma, 0, sizeof(*vma));
	vma->vm_mm = current->mm;
	vma->vm_start = SZ_1M;
	vma->vm_end = SZ_1M + length;
	vma->vm_pgoff = file_offset >> PAGE_SHIFT;
	vma->vm_ops = &fixture_vm_ops;
	vma_set_slice_off(vma, (file_offset >> MM_PAGE_SHIFT(current->mm)) &
			       PPPS_SLICE_MASK);
}

static bool fixture_folio_within_range(unsigned long file_offset)
{
	struct folio folio = {};
	struct vm_area_struct vma;

	fixture_init_vma(&vma, file_offset, PAGE_SIZE);
	folio.index = vma.vm_pgoff;
	return folio_within_range(&folio, &vma, vma.vm_start, vma.vm_end);
}

static bool fixture_vma_address_end(void)
{
	const unsigned long file_offset = SUBPAGE_OFFSET;
	struct page_vma_mapped_walk pvmw = {};
	struct vm_area_struct vma;
	unsigned long expected;
	unsigned long end;

	fixture_init_vma(&vma, file_offset, 2 * PAGE_SIZE);
	pvmw.vma = &vma;
	pvmw.pgoff = vma.vm_pgoff;
	pvmw.nr_pages = 1;
	pvmw.address = vma_address(&vma, pvmw.pgoff, pvmw.nr_pages);
	end = vma_address_end(&pvmw);
	expected = vma.vm_start + PAGE_SIZE - offset_in_page(file_offset);
	return pvmw.address == vma.vm_start && end == expected;
}

static long fixture_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	bool expected;
	bool within;

	switch (cmd) {
	case FOLIO_WITHIN_RANGE_PPPS_CHECK_ALIGNED:
		within = fixture_folio_within_range(0);
		expected = true;
		break;
	case FOLIO_WITHIN_RANGE_PPPS_CHECK_SUBPAGE:
		within = fixture_folio_within_range(SUBPAGE_OFFSET);
		expected = IS_ALIGNED(SUBPAGE_OFFSET, PAGE_SIZE);
		break;
	case FOLIO_WITHIN_RANGE_PPPS_CHECK_ADDRESS_END:
		return fixture_vma_address_end() ? 0 : -ERANGE;
	default:
		return -ENOTTY;
	}

	return within == expected ? 0 : -ERANGE;
}

static const struct file_operations fixture_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = fixture_ioctl,
};

PPPS_MISC_MODULE(DEVICE_NAME, &fixture_fops, 0666,
		 ppps_misc_no_setup, ppps_misc_no_teardown,
		 "folio_within_range PPPS regression fixture");
