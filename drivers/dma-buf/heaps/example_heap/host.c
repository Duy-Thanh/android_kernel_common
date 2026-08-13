// SPDX-License-Identifier: GPL-2.0

#include <asm/esr.h>
#include <asm/kvm_pkvm.h>
#include <asm/kvm_pkvm_module.h>
#include <asm/word-at-a-time.h>
#include <kunit/test.h>
#include <linux/dma-heap.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include "hyp/example_heap.h"

#ifndef MODULE
BUILD_BUG("Example pKVM heap must be compiled as a module");
#endif

#include <linux/dma-buf.h>
#include <linux/scatterlist.h>

unsigned long protect_page_hvc, unprotect_page_hvc;
static struct miscdevice pkvm_vendor_dev;

static int pkvm_vendor_protect_dmabuf(int fd, bool protect)
{
	struct dma_buf *dmabuf;
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct scatterlist *sg;
	int i, ret = 0;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	attach = dma_buf_attach(dmabuf, pkvm_vendor_dev.this_device);
	if (IS_ERR(attach)) {
		ret = PTR_ERR(attach);
		goto out_put;
	}
	attach->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	sgt = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto out_detach;
	}

	for_each_sgtable_sg(sgt, sg, i) {
		struct page *page = sg_page(sg);
		unsigned long pfn = page_to_pfn(page);
		unsigned long nr_pages = sg->length >> PAGE_SHIFT;
		unsigned long hvc = protect ? protect_page_hvc : unprotect_page_hvc;

		ret = pkvm_el2_mod_call(hvc, pfn, nr_pages);
		if (ret) {
			pr_err("pkvm_vendor: %sprotect failed for pfn 0x%lx (nr_pages %lu): %d\n",
			       protect ? "" : "un", pfn, nr_pages, ret);
			break;
		}
	}

	dma_buf_unmap_attachment(attach, sgt, DMA_BIDIRECTIONAL);
out_detach:
	dma_buf_detach(dmabuf, attach);
out_put:
	dma_buf_put(dmabuf);
	return ret;
}

static long pkvm_vendor_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	switch (ioctl) {
	case PKVM_VENDOR_IOCTL_ENABLE_SMC:
		return 0;
	case PKVM_VENDOR_IOCTL_PROTECT_DMABUF:
		return pkvm_vendor_protect_dmabuf((int)arg, true);
	case PKVM_VENDOR_IOCTL_UNPROTECT_DMABUF:
		return pkvm_vendor_protect_dmabuf((int)arg, false);
	default:
		return -ENOTSUPP;
	}
}

static struct file_operations pkvm_vendor_ops = {
	.unlocked_ioctl = pkvm_vendor_ioctl,
};

static struct miscdevice pkvm_vendor_dev = {
	MISC_DYNAMIC_MINOR,
	"pkvm_vendor",
	&pkvm_vendor_ops,
};

static int host_init(void)
{
	struct dma_heap_export_info exp_info;
	struct dma_heap *ex_heap;
	unsigned long token;
	int ret;

	exp_info.name = "example_heap";
	exp_info.ops = &system_heap_modified_ops;
	exp_info.priv = NULL;

	ex_heap = dma_heap_add(&exp_info);
	if (IS_ERR(ex_heap))
		return PTR_ERR(ex_heap);

	ret = pkvm_load_el2_module(__kvm_nvhe_hyp_init, &token);
	if (ret)
		return ret;

	protect_page_hvc = pkvm_register_el2_mod_call(__kvm_nvhe_protect_page, token);
	if (protect_page_hvc < 0)
		return -EINVAL;

	unprotect_page_hvc = pkvm_register_el2_mod_call(__kvm_nvhe_unprotect_page, token);
	if (unprotect_page_hvc < 0)
		return -EINVAL;

	ret = misc_register(&pkvm_vendor_dev);
	if (ret)
		return ret;

	dma_coerce_mask_and_coherent(pkvm_vendor_dev.this_device, DMA_BIT_MASK(64));
	return 0;
}
module_init(host_init);

MODULE_DESCRIPTION("Example pKVM dma-buf heap");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("DMA_BUF");
MODULE_IMPORT_NS("DMA_BUF_HEAP");
