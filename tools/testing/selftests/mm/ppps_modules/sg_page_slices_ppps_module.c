// SPDX-License-Identifier: GPL-2.0
/* SG byte-stream, chaining, ownership and invalid-slice contracts. */
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

#include "../sg_page_slices_ppps.h"
#include "../../ppps/ppps_misc_module.h"

static u8 pattern(unsigned int page, unsigned int offset)
{
	return (page * 71 + offset * 13 + (offset >> 8)) & 0xff;
}

static long check_slices(unsigned int test)
{
	struct page *backing[2] = {};
	struct sg_table table = {};
	struct page_span *spans = NULL;
	struct page **pages = NULL;
	struct scatterlist *sg;
	unsigned int count = test == SG_SLICES_CHAINED ?
		SG_MAX_SINGLE_ALLOC + 1 : 4;
	unsigned long size = PAGE_SIZE;
	u8 *actual = NULL, *expected = NULL;
	unsigned int i, j, pos;
	int wanted = test >= SG_SLICES_EMPTY_ARRAY ? -EINVAL : 0;
	long ret = -ENOMEM;

	pages = kcalloc(count, sizeof(*pages), GFP_KERNEL);
	spans = kcalloc(count, sizeof(*spans), GFP_KERNEL);
	if (!pages || !spans)
		goto out;
	for (i = 0; i < ARRAY_SIZE(backing); i++) {
		u8 *addr;

		backing[i] = alloc_page(GFP_KERNEL);
		if (!backing[i])
			goto out;
		addr = kmap_local_page(backing[i]);
		for (j = 0; j < PAGE_SIZE; j++)
			addr[j] = pattern(i, j);
		kunmap_local(addr);
	}
	for (i = 0; i < count; i++) {
		pages[i] = backing[0];
		spans[i].offset = (i % 4) * (PAGE_SIZE / 4);
		spans[i].length = PAGE_SIZE / 4;
	}
	switch (test) {
	case SG_SLICES_FULL_PAGE:
		count = 1;
		spans[0].length = PAGE_SIZE;
		break;
	case SG_SLICES_PARTIAL_PAGE:
		count = 1;
		spans[0].offset = 17;
		size = spans[0].length = PAGE_SIZE - 29;
		break;
	case SG_SLICES_CONTIGUOUS:
		break;
	case SG_SLICES_REORDERED:
		for (i = 0; i < count; i++)
			spans[i].offset = (3 - i) * (PAGE_SIZE / 4);
		break;
	case SG_SLICES_DISTINCT_PAGES:
		for (i = 0; i < count; i++)
			pages[i] = backing[i % 2];
		break;
	case SG_SLICES_OVERLAPPING:
		for (i = 0; i < count; i++)
			spans[i].offset = 19;
		break;
	case SG_SLICES_CHAINED:
		size = count;
		for (i = 0; i < count; i++) {
			spans[i].offset = i % PAGE_SIZE;
			spans[i].length = 1;
		}
		break;
	case SG_SLICES_EMPTY_ARRAY:
		count = 0;
		break;
	case SG_SLICES_EMPTY_SIZE:
		size = 0;
		break;
	case SG_SLICES_NULL_PAGE:
		pages[2] = NULL;
		break;
	case SG_SLICES_ZERO_LENGTH:
		spans[2].length = 0;
		break;
	case SG_SLICES_INVALID_OFFSET:
		spans[2].offset = PAGE_SIZE;
		break;
	case SG_SLICES_PAGE_OVERRUN:
		spans[2].offset = PAGE_SIZE - 1;
		spans[2].length = 2;
		break;
	case SG_SLICES_SIZE_TOO_SMALL:
		size--;
		break;
	case SG_SLICES_SIZE_TOO_LARGE:
		size++;
		break;
	case SG_SLICES_HUGE_LENGTH:
		spans[2].length = UINT_MAX;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}
	ret = sg_alloc_table_from_page_slices(&table, pages, spans, count,
					    size, GFP_KERNEL);
	if (ret != wanted) {
		if (!ret)
			sg_free_table(&table);
		ret = -ERANGE;
		goto out;
	}
	if (wanted) {
		ret = table.sgl ? -EUCLEAN : 0;
		goto refs;
	}
	ret = -ENOMEM;
	actual = kmalloc(size, GFP_KERNEL);
	expected = kmalloc(size, GFP_KERNEL);
	if (!actual || !expected)
		goto free_table;
	ret = -ENODATA;
	if (table.orig_nents != count || table.nents != count)
		goto free_table;
	pos = 0;
	for_each_sgtable_sg(&table, sg, i) {
		if (sg_page(sg) != pages[i] || sg->offset != spans[i].offset ||
		    sg->length != spans[i].length ||
		    !!sg_is_last(sg) != (i == count - 1))
			goto free_table;
		for (j = 0; j < spans[i].length; j++)
			expected[pos++] = pattern(pages[i] == backing[1],
						  spans[i].offset + j);
	}
	if (pos == size && sg_copy_to_buffer(table.sgl, table.orig_nents,
					    actual, size) == size &&
	    !memcmp(actual, expected, size))
		ret = 0;
free_table:
	sg_free_table(&table);
refs:
	/* SG construction/freeing must not consume or acquire page references. */
	for (i = 0; i < ARRAY_SIZE(backing); i++)
		if (page_ref_count(backing[i]) != 1)
			ret = -EUCLEAN;
out:
	kfree(actual);
	kfree(expected);
	kfree(pages);
	kfree(spans);
	for (i = 0; i < ARRAY_SIZE(backing); i++)
		if (backing[i])
			__free_page(backing[i]);
	return ret;
}

static long fixture_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	if (cmd != SG_PAGE_SLICES_PPPS_CHECK || arg >= SG_SLICES_CASES)
		return -EINVAL;
	return check_slices(arg);
}

static const struct file_operations fixture_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = fixture_ioctl,
};

PPPS_MISC_MODULE("sg_page_slices_ppps", &fixture_fops, 0600,
		 ppps_misc_no_setup, ppps_misc_no_teardown,
		 "Page-slice scatterlist contract tests");
