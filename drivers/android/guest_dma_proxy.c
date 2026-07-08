// SPDX-License-Identifier: GPL-2.0-only
/*
 * Guest DMA-Buf Proxy Driver
 *
 * This driver provides an ioctl interface for guest userspace to convert
 * hypervisor-injected physical address ranges (e.g., from host shared memory)
 * into standard dma-buf file descriptors.
 */

#include <linux/dma-buf.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/xarray.h>
#include <linux/file.h>
#include <uapi/linux/guest_dma_proxy.h>

struct guest_dma_proxy_device {
	struct miscdevice misc;
	struct device *dev;
	phys_addr_t reserved_base;
	phys_addr_t reserved_size;
	struct xarray imported_bufs;
	struct mutex lock;
};

struct guest_dma_proxy_buf {
	struct guest_dma_proxy_device *gdev;
	phys_addr_t paddr;
	size_t size;
};

static struct guest_dma_proxy_device *global_gdev;

/*
 * =======================================================================
 * Security Validation (Isolated & Pluggable)
 * =======================================================================
 *
 * This function is strictly isolated so that it can be easily updated or
 * replaced with a real pKVM hypercall or virtio control message in the future.
 */
static int verify_import_token(struct guest_dma_proxy_device *gdev,
			       u64 paddr, u64 size, const u8 *token)
{
	/* Basic bounds checking against the DTB reserved memory region */
	if (gdev->reserved_size > 0) {
		if (paddr < gdev->reserved_base ||
		    (paddr + size) > (gdev->reserved_base + gdev->reserved_size)) {
			dev_err(gdev->dev, "Requested region [%llx-%llx] outside reserved bounds\n",
				paddr, paddr + size);
			return -EPERM;
		}
	}

	/*
	 * TODO: Implement actual token validation with host/hypervisor here.
	 * e.g., pkvm_verify_shared_mem(paddr, size, token);
	 */

	return 0; /* Success by default for now */
}

/*
 * =======================================================================
 * dma_buf_ops implementation
 * =======================================================================
 */

static int guest_dma_proxy_attach(struct dma_buf *dmabuf,
				  struct dma_buf_attachment *attach)
{
	return 0;
}

static void guest_dma_proxy_detach(struct dma_buf *dmabuf,
				   struct dma_buf_attachment *attach)
{
}

static struct sg_table *guest_dma_proxy_map_dma_buf(struct dma_buf_attachment *attach,
						    enum dma_data_direction dir)
{
	struct guest_dma_proxy_buf *gbuf = attach->dmabuf->priv;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (ret) {
		kfree(sgt);
		return ERR_PTR(ret);
	}

	/*
	 * We populate the SG table with the physical address.
	 * Note: Depending on the architecture and IOMMU setup, we might need
	 * to use sg_set_page if pages exist, or just set the DMA address directly.
	 * For PFN mapped memory without struct pages, we set the DMA address.
	 */
	sg_dma_address(sgt->sgl) = gbuf->paddr;
	sg_dma_len(sgt->sgl) = gbuf->size;

	return sgt;
}

static void guest_dma_proxy_unmap_dma_buf(struct dma_buf_attachment *attach,
					  struct sg_table *sgt,
					  enum dma_data_direction dir)
{
	sg_free_table(sgt);
	kfree(sgt);
}

static int guest_dma_proxy_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct guest_dma_proxy_buf *gbuf = dmabuf->priv;

	/* Enforce read-only mapping as per the plan */
	if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
		return -EPERM;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot); /* or proper coherent prot */

	return remap_pfn_range(vma, vma->vm_start,
			       gbuf->paddr >> PAGE_SHIFT,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static void guest_dma_proxy_release(struct dma_buf *dmabuf)
{
	struct guest_dma_proxy_buf *gbuf = dmabuf->priv;
	struct guest_dma_proxy_device *gdev = gbuf->gdev;

	mutex_lock(&gdev->lock);
	/* Remove from tracking (using paddr as an example key/index) */
	/* Since xarray uses u32 indices, we'd need a more robust allocator for real IDs,
	 * but for this example we simply free the tracking struct.
	 */
	mutex_unlock(&gdev->lock);

	kfree(gbuf);
}

static const struct dma_buf_ops guest_dma_proxy_ops = {
	.attach = guest_dma_proxy_attach,
	.detach = guest_dma_proxy_detach,
	.map_dma_buf = guest_dma_proxy_map_dma_buf,
	.unmap_dma_buf = guest_dma_proxy_unmap_dma_buf,
	.mmap = guest_dma_proxy_mmap,
	.release = guest_dma_proxy_release,
};

/*
 * =======================================================================
 * IOCTL Interface
 * =======================================================================
 */

static long guest_dma_proxy_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct guest_dma_proxy_device *gdev = file->private_data;
	struct guest_dma_proxy_import_args args;
	struct guest_dma_proxy_buf *gbuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	struct dma_buf *dmabuf;
	int fd, ret;

	if (cmd != GUEST_DMA_PROXY_IOC_IMPORT)
		return -ENOTTY;

	if (copy_from_user(&args, (void __user *)arg, sizeof(args)))
		return -EFAULT;

	if (!PAGE_ALIGNED(args.paddr) || !PAGE_ALIGNED(args.size))
		return -EINVAL;

	/* 1. Security Validation */
	ret = verify_import_token(gdev, args.paddr, args.size, args.token);
	if (ret)
		return ret;

	/* 2. Allocate tracking structure */
	gbuf = kzalloc(sizeof(*gbuf), GFP_KERNEL);
	if (!gbuf)
		return -ENOMEM;

	gbuf->gdev = gdev;
	gbuf->paddr = args.paddr;
	gbuf->size = args.size;

	/* 3. Export dma-buf */
	exp_info.ops = &guest_dma_proxy_ops;
	exp_info.size = args.size;
	exp_info.flags = O_RDONLY | O_CLOEXEC; /* Enforce read-only */
	exp_info.priv = gbuf;

	fd = get_unused_fd_flags(exp_info.flags);
	if (fd < 0) {
		ret = fd;
		goto err_free_gbuf;
	}

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto err_put_fd;
	}

	args.fd = fd;
	if (copy_to_user((void __user *)arg, &args, sizeof(args))) {
		ret = -EFAULT;
		goto err_put_dmabuf;
	}

	fd_install(fd, dmabuf->file);
	return 0;

err_put_dmabuf:
	dma_buf_put(dmabuf);
err_put_fd:
	put_unused_fd(fd);
err_free_gbuf:
	kfree(gbuf);
	return ret;
}

static int guest_dma_proxy_open(struct inode *inode, struct file *file)
{
	file->private_data = global_gdev;
	return 0;
}

static const struct file_operations guest_dma_proxy_fops = {
	.owner = THIS_MODULE,
	.open = guest_dma_proxy_open,
	.unlocked_ioctl = guest_dma_proxy_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
};

/*
 * =======================================================================
 * Initialization and Probing
 * =======================================================================
 */

static int guest_dma_proxy_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct guest_dma_proxy_device *gdev;
	struct reserved_mem *rmem;
	int ret;

	gdev = devm_kzalloc(dev, sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	gdev->dev = dev;
	mutex_init(&gdev->lock);
	xa_init(&gdev->imported_bufs);

	/* Check for reserved memory region in DTB */
	if (np) {
		struct device_node *rmem_np;

		rmem_np = of_parse_phandle(np, "memory-region", 0);
		if (rmem_np) {
			rmem = of_reserved_mem_lookup(rmem_np);
			of_node_put(rmem_np);
			if (rmem) {
				gdev->reserved_base = rmem->base;
				gdev->reserved_size = rmem->size;
				dev_info(dev, "Found reserved memory region [%pa-%pa]\n",
					 &rmem->base, &rmem->size);
			}
		}
	}

	gdev->misc.minor = MISC_DYNAMIC_MINOR;
	gdev->misc.name = "guest_dma_proxy";
	gdev->misc.fops = &guest_dma_proxy_fops;
	gdev->misc.parent = dev;

	ret = misc_register(&gdev->misc);
	if (ret) {
		dev_err(dev, "Failed to register misc device\n");
		return ret;
	}

	global_gdev = gdev;
	platform_set_drvdata(pdev, gdev);

	dev_info(dev, "Guest DMA-Buf Proxy driver initialized\n");
	return 0;
}

static void guest_dma_proxy_remove(struct platform_device *pdev)
{
	struct guest_dma_proxy_device *gdev = platform_get_drvdata(pdev);

	misc_deregister(&gdev->misc);
	xa_destroy(&gdev->imported_bufs);
	global_gdev = NULL;
}

static const struct of_device_id guest_dma_proxy_of_match[] = {
	{ .compatible = "google,guest-dma-proxy", },
	{}
};
MODULE_DEVICE_TABLE(of, guest_dma_proxy_of_match);

static struct platform_driver guest_dma_proxy_driver = {
	.probe = guest_dma_proxy_probe,
	.remove = guest_dma_proxy_remove,
	.driver = {
		.name = "guest_dma_proxy",
		.of_match_table = guest_dma_proxy_of_match,
	},
};

module_platform_driver(guest_dma_proxy_driver);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Guest DMA-Buf Proxy Driver");
MODULE_LICENSE("GPL");
