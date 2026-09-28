// SPDX-License-Identifier: GPL-2.0

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/uaccess.h>

#include "../fault_in_ppps.h"

#include "../../ppps/ppps_misc_module.h"

static long fault_in_ppps_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct fault_in_ppps_args request;
	void __user *address;

	if (cmd != FAULT_IN_PPPS_IOCTL)
		return -ENOTTY;
	if (copy_from_user(&request, (void __user *)arg, sizeof(request)))
		return -EFAULT;
	if (!request.length || request.reserved)
		return -EINVAL;

	address = u64_to_user_ptr(request.address);
	switch (request.operation) {
	case FAULT_IN_PPPS_WRITEABLE:
		request.not_faulted = fault_in_writeable(address, request.length);
		break;
	case FAULT_IN_PPPS_SAFE_WRITEABLE:
		request.not_faulted = fault_in_safe_writeable(address, request.length);
		break;
	case FAULT_IN_PPPS_READABLE:
		request.not_faulted = fault_in_readable(address, request.length);
		break;
	default:
		return -EINVAL;
	}

	if (copy_to_user((void __user *)arg, &request, sizeof(request)))
		return -EFAULT;
	return 0;
}

static const struct file_operations fault_in_ppps_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = fault_in_ppps_ioctl,
	.compat_ioctl = fault_in_ppps_ioctl,
};

PPPS_MISC_MODULE(FAULT_IN_PPPS_DEVICE_NAME, &fault_in_ppps_fops, 0600,
		 ppps_misc_no_setup, ppps_misc_no_teardown,
		 "PPPS fault-in helper regression test");
