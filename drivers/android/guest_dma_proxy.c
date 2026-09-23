// SPDX-License-Identifier: GPL-2.0-only
/*
 * Guest DMA-Buf Proxy Driver
 *
 * This driver provides an ioctl interface for guest userspace to convert
 * hypervisor-injected physical address ranges (e.g., from host shared memory)
 * into standard dma-buf file descriptors.
 */

#include <linux/dma-buf.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/interval_tree_generic.h>
#include <linux/ktime.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#ifdef CONFIG_ARM64
#include <asm/hypervisor.h>
#include <linux/arm-smccc.h>
#endif
#include <uapi/linux/guest_dma_proxy.h>

struct guest_dma_proxy_device {
	struct miscdevice misc;
	phys_addr_t reserved_base;
	phys_addr_t reserved_size;
	struct mutex lock;
	struct rb_root_cached itree;
};

struct guest_dma_proxy_buf {
	struct guest_dma_proxy_device *gdev;
	phys_addr_t paddr;
	size_t size;
	struct rb_node rb;
	phys_addr_t __subtree_last;
};

#define START(buf) ((buf)->paddr)
#define LAST(buf) ((buf)->paddr + (buf)->size - 1)

INTERVAL_TREE_DEFINE(struct guest_dma_proxy_buf,
		     rb, phys_addr_t, __subtree_last,
		     START, LAST, static inline, guest_dma_proxy_itree);

/*
 * =======================================================================
 * Security Validation (Isolated & Pluggable)
 * =======================================================================
 */
static int verify_import_token(struct guest_dma_proxy_device *gdev,
			       u64 paddr, u64 size, const u8 *token)
{
	phys_addr_t res_end;
	u64 end;

	/*
	 * TODO(b/559819568): Make guest DMA proxy driver hypervisor agnostic.
	 * TODO(b/515757062): Validate token via hypercall or virtio control
	 * message if hypervisor-level security attestation is implemented.
	 */
	if (!size)
		return -EINVAL;

	if (check_add_overflow(paddr, size, &end))
		return -EINVAL;

	if (!gdev->reserved_size)
		return -EPERM;

	res_end = gdev->reserved_base + gdev->reserved_size;
	if (paddr < gdev->reserved_base || end > res_end) {
		pr_err_ratelimited("guest_dma_proxy: Requested region [0x%llx-0x%llx] outside reserved DTB bounds [0x%pa-0x%pa]\n",
				   paddr, end, &gdev->reserved_base, &res_end);
		return -EPERM;
	}

	return 0;
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
	unsigned long pfn;

	/* Enforce read-only mapping */
	if (vma->vm_flags & (VM_WRITE | VM_MAYWRITE))
		return -EPERM;

	/*
	 * TODO(b/515757062): Support cache-coherent prot if CPU cache snooping
	 * is negotiated between host and guest.
	 * Use pgprot_writecombine (MT_NORMAL_NC on ARM64) to allow unaligned
	 * accesses and NEON/SIMD instructions without alignment faults.
	 */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	pfn = (gbuf->paddr >> PAGE_SHIFT) + vma->vm_pgoff;

	return remap_pfn_range(vma, vma->vm_start,
			       pfn,
			       vma->vm_end - vma->vm_start,
			       vma->vm_page_prot);
}

static void guest_dma_proxy_release(struct dma_buf *dmabuf)
{
	struct guest_dma_proxy_buf *gbuf = dmabuf->priv;

#ifdef CONFIG_ARM64
	/*
	 * TODO(b/559819568): Make page relinquishment hypervisor agnostic.
	 * Relinquish pages back to the Host without poisoning if supported.
	 */
	if (gbuf->size > 0 &&
	    kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_MEM_RELINQUISH)) {
		u64 flags = KVM_FUNC_MEM_RELINQUISH_NO_POISON;
		phys_addr_t paddr = gbuf->paddr;
		phys_addr_t end = paddr + gbuf->size;
		struct arm_smccc_res res;
		u64 count = 0;
		ktime_t start, delta;

		pr_info("guest_dma_proxy: Relinquishing range [%pa - %pa]\n",
			&paddr, &end);
		start = ktime_get();
		while (paddr < end) {
			arm_smccc_1_1_invoke(ARM_SMCCC_VENDOR_HYP_KVM_MEM_RELINQUISH_FUNC_ID,
					     paddr, 0, flags, &res);
			if ((long)res.a0 == SMCCC_RET_INVALID_PARAMETER &&
			    flags == KVM_FUNC_MEM_RELINQUISH_NO_POISON) {
				flags = 0;
				arm_smccc_1_1_invoke(
					ARM_SMCCC_VENDOR_HYP_KVM_MEM_RELINQUISH_FUNC_ID,
					paddr, 0, flags, &res);
			}
			if (res.a0 != 0) {
				pr_err_ratelimited("guest_dma_proxy: Relinquish HVC failed for paddr %pa: %ld\n",
						   &paddr, (long)res.a0);
			} else {
				count++;
			}
			paddr += PAGE_SIZE;
			cond_resched();
		}
		delta = ktime_sub(ktime_get(), start);
		pr_info("guest_dma_proxy: Relinquished %llu pages in %lld us (flags=0x%llx)\n",
			count, ktime_to_us(delta), flags);
	}
#endif

	mutex_lock(&gbuf->gdev->lock);
	guest_dma_proxy_itree_remove(gbuf, &gbuf->gdev->itree);
	mutex_unlock(&gbuf->gdev->lock);

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

	if (args.flags != 0)
		return -EINVAL;

	if (args.reserved[0] != 0 || args.reserved[1] != 0)
		return -EINVAL;

	if (!args.size || !PAGE_ALIGNED(args.paddr) || !PAGE_ALIGNED(args.size))
		return -EINVAL;

	/* 1. Security Validation */
	ret = verify_import_token(gdev, args.paddr, args.size, args.token);
	if (ret)
		return ret;

	/* 2. Validate non-overlapping range across active imported buffers */
	mutex_lock(&gdev->lock);
	if (guest_dma_proxy_itree_iter_first(&gdev->itree, args.paddr,
					     args.paddr + args.size - 1)) {
		pr_err_ratelimited("guest_dma_proxy: Requested range [0x%llx - 0x%llx] overlaps active buffer\n",
				   args.paddr, args.paddr + args.size);
		mutex_unlock(&gdev->lock);
		return -EBUSY;
	}

	/* 3. Allocate tracking structure */
	gbuf = kzalloc(sizeof(*gbuf), GFP_KERNEL);
	if (!gbuf) {
		mutex_unlock(&gdev->lock);
		return -ENOMEM;
	}

	gbuf->gdev = gdev;
	gbuf->paddr = args.paddr;
	gbuf->size = args.size;
	guest_dma_proxy_itree_insert(gbuf, &gdev->itree);
	mutex_unlock(&gdev->lock);

	/* 4. Export dma-buf */
	exp_info.ops = &guest_dma_proxy_ops;
	exp_info.size = args.size;
	exp_info.flags = O_RDONLY | O_CLOEXEC; /* Enforce read-only */
	exp_info.priv = gbuf;

	fd = get_unused_fd_flags(exp_info.flags);
	if (fd < 0) {
		ret = fd;
		goto err_remove_node;
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
	/*
	 * dma_buf_put drops the final reference to dmabuf, which triggers
	 * guest_dma_proxy_release to remove the interval node and free gbuf.
	 * Do not cascade to err_put_fd or err_remove_node to avoid double cleanup.
	 */
	dma_buf_put(dmabuf);
	put_unused_fd(fd);
	return ret;

err_put_fd:
	put_unused_fd(fd);
err_remove_node:
	mutex_lock(&gdev->lock);
	guest_dma_proxy_itree_remove(gbuf, &gdev->itree);
	mutex_unlock(&gdev->lock);
	kfree(gbuf);
	return ret;
}

static int guest_dma_proxy_open(struct inode *inode, struct file *file)
{
	struct guest_dma_proxy_device *gdev =
		container_of(file->private_data, struct guest_dma_proxy_device, misc);

	file->private_data = gdev;
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
 * Platform Driver Probing and Initialization
 * =======================================================================
 */

static int guest_dma_proxy_probe(struct platform_device *pdev)
{
	struct guest_dma_proxy_device *gdev;
	struct device *dev = &pdev->dev;
	struct device_node *rmem_np;
	struct resource r;
	int ret;

	rmem_np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!rmem_np) {
		dev_err(dev, "Missing memory-region phandle\n");
		return -ENODEV;
	}

	if (!of_property_read_bool(rmem_np, "no-map")) {
		dev_err(dev, "Reserved memory node must have 'no-map' property\n");
		of_node_put(rmem_np);
		return -EINVAL;
	}

	ret = of_address_to_resource(rmem_np, 0, &r);
	of_node_put(rmem_np);
	if (ret || !resource_size(&r)) {
		dev_err(dev, "Failed to resolve valid reserved memory resource: %d\n",
			ret);
		return ret ? ret : -EINVAL;
	}

	gdev = devm_kzalloc(dev, sizeof(*gdev), GFP_KERNEL);
	if (!gdev)
		return -ENOMEM;

	mutex_init(&gdev->lock);
	gdev->itree = RB_ROOT_CACHED;
	gdev->reserved_base = r.start;
	gdev->reserved_size = resource_size(&r);
	dev_info(dev, "Found DTB reserved memory region [0x%pa - 0x%pa]\n",
		 &gdev->reserved_base,
		 &(phys_addr_t){gdev->reserved_base + gdev->reserved_size});

	gdev->misc.minor = MISC_DYNAMIC_MINOR;
	gdev->misc.name = "guest_dma_proxy";
	gdev->misc.fops = &guest_dma_proxy_fops;
	gdev->misc.parent = dev;

	platform_set_drvdata(pdev, gdev);

	ret = misc_register(&gdev->misc);
	if (ret) {
		dev_err(dev, "Failed to register misc device: %d\n", ret);
		return ret;
	}

	dev_info(dev, "Guest DMA-Buf Proxy driver initialized\n");
	return 0;
}

static void guest_dma_proxy_remove(struct platform_device *pdev)
{
	struct guest_dma_proxy_device *gdev = platform_get_drvdata(pdev);

	misc_deregister(&gdev->misc);
}

static const struct of_device_id guest_dma_proxy_of_match[] = {
	{ .compatible = "android,guest-dma-proxy" },
	{}
};
MODULE_DEVICE_TABLE(of, guest_dma_proxy_of_match);

static struct platform_driver guest_dma_proxy_driver = {
	.probe	= guest_dma_proxy_probe,
	.remove	= guest_dma_proxy_remove,
	.driver	= {
		.name = "guest_dma_proxy",
		.of_match_table = guest_dma_proxy_of_match,
	},
};

static int __init guest_dma_proxy_init(void)
{
#ifdef CONFIG_ARM64
	/*
	 * Probing ARM_SMCCC_KVM_FUNC_MEM_RELINQUISH ensures we only register on
	 * pKVM protected VMs where hypervisor page relinquishment is active.
	 *
	 * TODO(b/559819568): Make capability detection hypervisor-agnostic once
	 * a common hypervisor discovery or DT property is established for AVF.
	 */
	if (!kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_MEM_RELINQUISH))
		return 0;
#endif

	return platform_driver_register(&guest_dma_proxy_driver);
}

static void __exit guest_dma_proxy_exit(void)
{
#ifdef CONFIG_ARM64
	if (!kvm_arm_hyp_service_available(ARM_SMCCC_KVM_FUNC_MEM_RELINQUISH))
		return;
#endif

	platform_driver_unregister(&guest_dma_proxy_driver);
}

device_initcall(guest_dma_proxy_init);
module_exit(guest_dma_proxy_exit);

MODULE_AUTHOR("Google LLC");
MODULE_DESCRIPTION("Guest DMA-Buf Proxy Driver");
MODULE_LICENSE("GPL");
