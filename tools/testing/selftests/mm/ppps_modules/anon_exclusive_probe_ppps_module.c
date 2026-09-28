// SPDX-License-Identifier: GPL-2.0

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "../anon_exclusive_probe_ppps.h"

static long
anon_exclusive_probe_ppps_ioctl(struct file *file, unsigned int command,
				unsigned long argument)
{
	struct anon_exclusive_probe_ppps probe;
	struct page *page;
	long nr;

	if (command != ANON_EXCLUSIVE_PROBE_PPPS_IOCTL)
		return -ENOTTY;
	if (copy_from_user(&probe, (void __user *)argument, sizeof(probe)))
		return -EFAULT;

	nr = get_user_pages_fast(probe.address, 1, 0, &page);
	if (nr != 1)
		return nr < 0 ? nr : -EFAULT;
	probe.pfn = page_to_pfn(page);
	probe.anon = PageAnon(page);
	probe.exclusive = probe.anon && PageAnonExclusive(page);
	probe.packed = folio_test_ppps_compat_anon(page_folio(page));
	put_page(page);

	if (copy_to_user((void __user *)argument, &probe, sizeof(probe)))
		return -EFAULT;
	return 0;
}

static const struct file_operations anon_exclusive_probe_ppps_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = anon_exclusive_probe_ppps_ioctl,
	.compat_ioctl = anon_exclusive_probe_ppps_ioctl,
};

static struct miscdevice anon_exclusive_probe_ppps_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "anon_exclusive_probe_ppps",
	.fops = &anon_exclusive_probe_ppps_fops,
	.mode = 0600,
};

static int __init anon_exclusive_probe_ppps_init(void)
{
	return misc_register(&anon_exclusive_probe_ppps_misc);
}

static void __exit anon_exclusive_probe_ppps_exit(void)
{
	misc_deregister(&anon_exclusive_probe_ppps_misc);
}

module_init(anon_exclusive_probe_ppps_init);
module_exit(anon_exclusive_probe_ppps_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Observe PPPS anonymous-page exclusivity after swapoff");
