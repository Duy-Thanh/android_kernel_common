// SPDX-License-Identifier: GPL-2.0

#include <linux/dma-mapping.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-sg.h>
#include <media/videobuf2-vmalloc.h>

#include "../../ppps/ppps_misc_module.h"

#define USER_PAGE_SIZE 4096UL
#define VB2_USERPTR_VMALLOC 1
#define VB2_USERPTR_DMA_SG 2
#define VB2_USERPTR_CAPTURE 3

struct vb2_userptr_request {
	__u64 user_addr;
	__u32 length;
	__u32 backend;
};

#define VB2_USERPTR_PPPS_RUN \
	_IOW('V', 0x71, struct vb2_userptr_request)

static struct miscdevice ppps_misc_device;

static int run_vmalloc(const struct vb2_userptr_request *req)
{
	struct vb2_queue q = { .dma_dir = DMA_BIDIRECTIONAL };
	struct vb2_buffer vb = { .vb2_queue = &q };
	const struct vb2_mem_ops *ops = &vb2_vmalloc_memops;
	unsigned char *vaddr;
	void *priv;
	int ret = 0;

	priv = ops->get_userptr(&vb, ppps_misc_device.this_device,
				(unsigned long)req->user_addr, req->length);
	if (IS_ERR(priv))
		return PTR_ERR(priv);
	if (ops->prepare)
		ops->prepare(priv);
	vaddr = ops->vaddr(&vb, priv);
	if (!vaddr || vaddr[0] != 0x11 ||
	    vaddr[USER_PAGE_SIZE] != 0x22) {
		ret = -EIO;
	} else {
		vaddr[0] = 0x31;
		vaddr[USER_PAGE_SIZE] = 0x42;
	}
	if (ops->finish)
		ops->finish(priv);
	ops->put_userptr(priv);
	return ret;
}

static int run_dma_sg(const struct vb2_userptr_request *req)
{
	struct vb2_queue q = { .dma_dir = DMA_BIDIRECTIONAL };
	struct vb2_buffer vb = { .vb2_queue = &q };
	const struct vb2_mem_ops *ops = &vb2_dma_sg_memops;
	struct sg_table *sgt;
	unsigned char *data;
	void *priv;
	int ret = 0;

	data = kmalloc(req->length, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	priv = ops->get_userptr(&vb, ppps_misc_device.this_device,
				(unsigned long)req->user_addr, req->length);
	if (IS_ERR(priv)) {
		ret = PTR_ERR(priv);
		goto out_free;
	}
	sgt = ops->cookie(&vb, priv);
	if (!sgt || sg_pcopy_to_buffer(sgt->sgl, sgt->orig_nents, data,
				       req->length, 0) != req->length ||
	    data[0] != 0x51 || data[USER_PAGE_SIZE] != 0x62) {
		ret = -EIO;
		goto out_put;
	}
	data[0] = 0x71;
	data[USER_PAGE_SIZE] = 0x82;
	if (sg_pcopy_from_buffer(sgt->sgl, sgt->orig_nents, data,
				 req->length, 0) != req->length)
		ret = -EIO;
out_put:
	ops->put_userptr(priv);
out_free:
	kfree(data);
	return ret;
}

/* A short capture followed by a cancelled capture on the same buffer. */
static int run_capture(const struct vb2_userptr_request *req)
{
	struct vb2_queue q = { .dma_dir = DMA_FROM_DEVICE };
	struct vb2_buffer vb = { .vb2_queue = &q };
	const struct vb2_mem_ops *ops = &vb2_vmalloc_memops;
	unsigned char __user *user = (void __user *)(unsigned long)req->user_addr;
	unsigned char *vaddr;
	void *priv;
	int ret = 0;

	priv = ops->get_userptr(&vb, ppps_misc_device.this_device,
				(unsigned long)req->user_addr, req->length);
	if (IS_ERR(priv))
		return PTR_ERR(priv);
	ops->prepare(priv);
	vaddr = ops->vaddr(&vb, priv);
	if (!vaddr) {
		ret = -EIO;
		goto out;
	}
	vaddr[0] = 0x31; /* The device only produces one byte. */
	ops->finish(priv);
	/* Userspace may edit the dequeued buffer before it is queued again. */
	if (put_user(0x5a, user + USER_PAGE_SIZE)) {
		ret = -EFAULT;
		goto out;
	}
	ops->prepare(priv);
	ops->finish(priv); /* Cancelled: the device did not write anything. */
out:
	ops->put_userptr(priv);
	return ret;
}

static long test_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vb2_userptr_request req;

	if (cmd != VB2_USERPTR_PPPS_RUN)
		return -ENOTTY;
	if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
		return -EFAULT;
	if (req.length != 2 * USER_PAGE_SIZE)
		return -EINVAL;

	switch (req.backend) {
	case VB2_USERPTR_VMALLOC:
		return run_vmalloc(&req);
	case VB2_USERPTR_DMA_SG:
		return run_dma_sg(&req);
	case VB2_USERPTR_CAPTURE:
		return run_capture(&req);
	default:
		return -EINVAL;
	}
}

static const struct file_operations test_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = test_ioctl,
};

/* The DMA-SG backend allocates against the misc device itself. */
static int test_set_dma_mask(struct miscdevice *device)
{
	return dma_coerce_mask_and_coherent(device->this_device,
					    DMA_BIT_MASK(64));
}

PPPS_MISC_MODULE_POST("vb2_userptr_ppps", &test_fops, 0600,
		      ppps_misc_no_setup, test_set_dma_mask,
		      ppps_misc_no_teardown,
		      "videobuf2 USERPTR PPPS regression fixture");
