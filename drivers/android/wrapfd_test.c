/* SPDX-License-Identifier: GPL-2.0-only */
/* wrapfd.c
 *
 * Wrapfd
 *
 * Copyright (C) 2025 Google, Inc.
 */

#include <linux/anon_inodes.h>
#include <linux/dma-buf.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <uapi/linux/wrapfd_test.h>

#include "wrapfd.h"

static struct miscdevice wrapfd_test_misc;

struct wrap_test_ctx {
	struct dma_buf *dmabuf;
	struct file *file;
	struct dma_buf_attachment *attachment;
	struct sg_table *table;
};

static int wrap_test_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct wrap_test_ctx *ctx = file->private_data;
	struct sg_table *table;
	unsigned long addr = vma->vm_start;
	unsigned long pgoff = vma->vm_pgoff;
	struct scatterlist *sg;
	int i, ret;

	if (!ctx->table) {
		struct dma_buf_attachment *attachment;

		attachment = dma_buf_attach(ctx->dmabuf,
					    wrapfd_test_misc.this_device);
		if (IS_ERR(attachment))
			return PTR_ERR(attachment);

		table = dma_buf_map_attachment_unlocked(attachment,
							DMA_BIDIRECTIONAL);
		if (IS_ERR(table)) {
			dma_buf_detach(ctx->dmabuf, attachment);
			return PTR_ERR(table);
		}
		dma_buf_mangle_sg_table(table);
		ctx->attachment = attachment;
		ctx->table = table;
	} else {
		table = ctx->table;
	}

	dma_buf_begin_cpu_access(ctx->dmabuf, DMA_FROM_DEVICE);
	for_each_sgtable_sg(table, sg, i) {
		unsigned long n = sg->length >> PAGE_SHIFT;

		if (pgoff < n)
			break;
		pgoff -= n;
	}

	for (; sg && addr < vma->vm_end; sg = sg_next(sg)) {
		unsigned long n = (sg->length >> PAGE_SHIFT) - pgoff;
		struct page *page = sg_page(sg) + pgoff;
		unsigned long size = n << PAGE_SHIFT;

		if (addr + size > vma->vm_end)
			size = vma->vm_end - addr;

		ret = remap_pfn_range(vma, addr, page_to_pfn(page),
				size, vma->vm_page_prot);
		if (ret)
			return ret;

		addr += size;
		pgoff = 0;
	}
	dma_buf_end_cpu_access(ctx->dmabuf, DMA_FROM_DEVICE);

	return 0;
}

static int wrap_test_release(struct inode *ignored, struct file *file)
{
	struct wrap_test_ctx *ctx = file->private_data;
	union wrapfd_mappable mappable;
	int ret = 0;

	if (!ctx)
		return -ENOENT;

	if (ctx->dmabuf) {
		if (ctx->table) {
			dma_buf_unmap_attachment_unlocked(ctx->attachment,
							  ctx->table,
							  DMA_BIDIRECTIONAL);
			dma_buf_detach(ctx->dmabuf, ctx->attachment);
		}
		mappable.dmabuf = ctx->dmabuf;
		ret = wrapfd_put(ctx->file, wrapfd_test_misc.this_device,
				 &mappable);
		fput(ctx->file);
	}

	kfree(ctx);

	return ret;
}

static const struct file_operations wrap_test_fops = {
	.owner		= THIS_MODULE,
	.mmap		= wrap_test_mmap,
	.release	= wrap_test_release,
};

static int get_wrapfd(unsigned long arg)
{
	struct wrapfd_test_get __user *user_wrapfd_test_get;
	struct wrapfd_test_get wrapfd_test_get;
	struct wrap_test_ctx *ctx;
	union wrapfd_mappable mappable;
	struct file *file;
	int ret;

	user_wrapfd_test_get = (struct wrapfd_test_get __user *)arg;
	if (copy_from_user(&wrapfd_test_get, user_wrapfd_test_get,
			   sizeof(wrapfd_test_get)))
		return -EFAULT;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	file = fget(wrapfd_test_get.wrapfd);
	if (!file) {
		kfree(ctx);
		return -EBADF;
	}

	ret = wrapfd_get(file, wrapfd_test_misc.this_device, &mappable);
	if (ret) {
		fput(file);
		kfree(ctx);
		return ret;
	}
	ctx->dmabuf = mappable.dmabuf;

	ret = anon_inode_getfd("[wrapfd_test]", &wrap_test_fops, ctx,
			       wrapfd_test_get.prot);
	if (ret < 0) {
		wrapfd_put(file, wrapfd_test_misc.this_device, &mappable);
		fput(file);
		kfree(ctx);
		return ret;
	}
	ctx->file = file;

	return ret;
}

static long wrapfd_test_dev_ioctl(struct file *file, unsigned int cmd,
				  unsigned long arg)
{
	int ret;

	VM_WARN_ON_ONCE(!current->mm);

	switch (cmd) {
	case WRAPFD_TEST_DEV_GET:
		ret = get_wrapfd(arg);
		break;
	default:
		return -ENOTTY;
	}

	return ret;
}

static const struct file_operations wrapfd_test_dev_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = wrapfd_test_dev_ioctl,
	.compat_ioctl = wrapfd_test_dev_ioctl,
	.llseek = noop_llseek,
};

static struct miscdevice wrapfd_test_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "wrapfd_test",
	.fops = &wrapfd_test_dev_fops,
};

static int __init wrapfd_test_init(void)
{
	int ret;

	ret = misc_register(&wrapfd_test_misc);
	if (ret) {
		pr_err("failed to register misc device!\n");
		return ret;
	}
	dma_coerce_mask_and_coherent(wrapfd_test_misc.this_device,
				     DMA_BIT_MASK(64));
	wrapfd_test_misc.this_device->bus_dma_limit = DMA_BIT_MASK(64);

	pr_info("wrapfd_test initialized\n");

	return 0;
}
device_initcall(wrapfd_test_init);
