// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/dma-heap.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/trusty/trusty.h>
#include <linux/vmalloc.h>

static struct dma_heap *trusty_heap;
static struct device *trusty_heap_dev;
static struct device *trusty_dev;

struct trusty_heap_buffer {
	struct dma_heap *heap;
	unsigned long len;
	struct sg_table sg_table;
	u64 mem_id;
};

static struct sg_table *trusty_heap_map_dma_buf(struct dma_buf_attachment *attachment,
						enum dma_data_direction direction)
{
	struct trusty_heap_buffer *buffer = attachment->dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	int ret;

	ret = dma_map_sgtable(attachment->dev, table, direction, 0);
	if (ret)
		return ERR_PTR(ret);

	return table;
}

static void trusty_heap_unmap_dma_buf(struct dma_buf_attachment *attachment,
				      struct sg_table *table,
				      enum dma_data_direction direction)
{
	dma_unmap_sgtable(attachment->dev, table, direction, 0);
}

static int trusty_heap_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct trusty_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	struct sg_page_iter piter;
	int ret;

	for_each_sgtable_page(table, &piter, vma->vm_pgoff) {
		struct page *page = sg_page_iter_page(&piter);

		ret = remap_pfn_range(vma, addr, page_to_pfn(page), PAGE_SIZE,
				      vma->vm_page_prot);
		if (ret)
			return ret;
		addr += PAGE_SIZE;
		if (addr >= vma->vm_end)
			return 0;
	}
	return 0;
}

static void trusty_heap_dma_buf_release(struct dma_buf *dmabuf)
{
	struct trusty_heap_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	struct scatterlist *sg;
	int i, ret;

	ret = trusty_reclaim_memory(trusty_dev, buffer->mem_id,
				    buffer->sg_table.sgl,
				    buffer->sg_table.orig_nents);
	if (ret) {
		dev_warn(trusty_heap_dev,
			 "trusty_reclaim_memory failed: handle: 0x%llx done\n",
			 buffer->mem_id);
	}

	for_each_sgtable_sg(table, sg, i) {
		__free_page(sg_page(sg));
	}
	sg_free_table(table);
	kfree(buffer);
}

static const struct dma_buf_ops trusty_heap_buf_ops = {
	.map_dma_buf = trusty_heap_map_dma_buf,
	.unmap_dma_buf = trusty_heap_unmap_dma_buf,
	.mmap = trusty_heap_mmap,
	.release = trusty_heap_dma_buf_release,
};

static struct dma_buf *trusty_heap_allocate(struct dma_heap *heap,
					    unsigned long len,
					    unsigned long fd_flags,
					    unsigned long heap_flags)
{
	struct trusty_heap_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned long size_remaining = len;
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	int i, ret = -ENOMEM;
	u64 id;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	buffer->heap = heap;
	buffer->len = len;

	INIT_LIST_HEAD(&pages);
	i = 0;
	while (size_remaining > 0) {
		/*
		 * Avoid trying to allocate memory if the process
		 * has been killed by SIGKILL
		 */
		if (fatal_signal_pending(current))
			goto free_buffer;

		page = alloc_page(GFP_KERNEL);
		if (!page)
			goto free_buffer;

		list_add_tail(&page->lru, &pages);
		size_remaining -= page_size(page);
		i++;
	}

	table = &buffer->sg_table;
	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_buffer;

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	/* create the dmabuf */
	exp_info.exp_name = dma_heap_get_name(heap);
	exp_info.ops = &trusty_heap_buf_ops;
	exp_info.size = buffer->len;
	exp_info.flags = fd_flags;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}

	ret = trusty_transfer_memory(trusty_dev, &id, table->sgl, table->nents,
				     PAGE_KERNEL, 0, false);
	if (ret)
		dev_err(trusty_heap_dev, "trusty_transfer_memory failed\n");

	buffer->mem_id = id;

	return dmabuf;

free_pages:
	for_each_sgtable_sg(table, sg, i) {
		__free_page(sg_page(sg));
	}
	sg_free_table(table);
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		__free_page(page);
	kfree(buffer);

	return ERR_PTR(ret);
}

static inline bool is_trusty_dma_buf(struct dma_buf *dma_buf)
{
	return dma_buf->ops == &trusty_heap_buf_ops;
}

int trusty_dma_buf_get_shared_mem_id(struct dma_buf *dma_buf,
				     trusty_shared_mem_id_t *id)
{
	struct trusty_heap_buffer *buffer = dma_buf->priv;

	if (is_trusty_dma_buf(dma_buf)) {
		*id = buffer->mem_id;
		return 0;
	}

	return -ENODATA;
}
EXPORT_SYMBOL_GPL(trusty_dma_buf_get_shared_mem_id);

static const struct dma_heap_ops trusty_heap_ops = {
	.allocate = trusty_heap_allocate,
};

static int trusty_heap_probe(struct platform_device *pdev)
{
	struct dma_heap_export_info exp_info;

	exp_info.name = "trusty";
	exp_info.ops = &trusty_heap_ops;
	exp_info.priv = NULL;

	trusty_heap = dma_heap_add(&exp_info);
	if (IS_ERR(trusty_heap))
		return PTR_ERR(trusty_heap);

	trusty_heap_dev = &pdev->dev;
	trusty_dev = pdev->dev.parent;

	return 0;
}

static const struct of_device_id trusty_heap_of_match[] = {
	{ .compatible = "android,trusty-dma-heap-v1", },
	{},
};

MODULE_DEVICE_TABLE(trusty, trusty_heap_of_match);

static struct platform_driver trusty_heap_driver = {
	.probe = trusty_heap_probe,
	.driver = {
		.name = "trusty-heap",
		.of_match_table = trusty_heap_of_match,
	},
};

module_platform_driver(trusty_heap_driver);

MODULE_LICENSE("GPL v2");
