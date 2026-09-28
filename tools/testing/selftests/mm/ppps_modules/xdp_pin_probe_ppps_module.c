// SPDX-License-Identifier: GPL-2.0

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/uaccess.h>

struct xdp_pin_probe {
	__u64 address[2];
	__u32 pinned[2];
	__u32 order[2];
	__s64 refs[2];
	__u64 pfn[2];
};

#define XDP_PIN_PROBE_IOCTL _IOWR(0xa4, 1, struct xdp_pin_probe)

static long xdp_pin_probe_ioctl(struct file *file, unsigned int command,
				unsigned long argument)
{
	struct xdp_pin_probe probe;
	unsigned int i;

	if (command != XDP_PIN_PROBE_IOCTL)
		return -ENOTTY;
	if (copy_from_user(&probe, (void __user *)argument, sizeof(probe)))
		return -EFAULT;

	for (i = 0; i < ARRAY_SIZE(probe.address); i++) {
		struct folio *folio;
		struct page *page;
		long nr;

		nr = get_user_pages_fast(probe.address[i], 1, 0, &page);
		if (nr != 1)
			return nr < 0 ? nr : -EFAULT;
		folio = page_folio(page);
		probe.pinned[i] = folio_maybe_dma_pinned(folio);
		probe.order[i] = folio_order(folio);
		probe.refs[i] = folio_ref_count(folio);
		probe.pfn[i] = page_to_pfn(page);
		put_page(page);
	}

	if (copy_to_user((void __user *)argument, &probe, sizeof(probe)))
		return -EFAULT;
	return 0;
}

static const struct file_operations xdp_pin_probe_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = xdp_pin_probe_ioctl,
	.compat_ioctl = xdp_pin_probe_ioctl,
};

static struct miscdevice xdp_pin_probe_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xdp_pin_probe_ppps",
	.fops = &xdp_pin_probe_fops,
	.mode = 0600,
};

static int __init xdp_pin_probe_init(void)
{
	return misc_register(&xdp_pin_probe_misc);
}

static void __exit xdp_pin_probe_exit(void)
{
	misc_deregister(&xdp_pin_probe_misc);
}

module_init(xdp_pin_probe_init);
module_exit(xdp_pin_probe_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Observe which PPPS backing page AF_XDP pins");
