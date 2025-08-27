// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025 Samsung Electronics Co., Ltd.
 *            http://www.samsung.com/
 */

#include <linux/debugfs.h>

#include "exynos-hvc.h"
#include "hvm_drv.h"

/* hvm debugfs root directory */
struct dentry *hvm_debugfs_dir;

static ssize_t translate_addr_ops_get_pa(struct file *file, char __user *buf,
					size_t len, loff_t *offset)
{
	char tmp[64] = {0, };
	struct hvm *vm = file->private_data;
	u64 ipa, pa;
	u64 ret;

	if (*offset == 0) {
		// get translated pa
		ipa = vm->vm_debug.ipa_requested;
		if (ipa == -EINVAL) {
			snprintf(tmp, sizeof(tmp),
				"Invalid address: Incorrect format, [a-fA-F0-9] only\n");
			goto exit;
		}

		ret = exynos_hvc(HVC_FID_HVM_DEBUG, HVM_DEBUG_TRANSLATE_IPA2PA,
				vm->vm_id, ipa, 0);

		if (ret == -ENOMEM) {
			snprintf(tmp, sizeof(tmp),
				"Invalid address 0x%llx: Outside of vm memory\n", ipa);
			goto exit;
		} else if (ret == -EFAULT) {
			snprintf(tmp, sizeof(tmp),
				"Invalid address 0x%llx: Not mapped\n", ipa);
			goto exit;
		}

		pa = ret;

		// Is ipa shared with host?
		ret = exynos_hvc(HVC_FID_HVM_DEBUG, HVM_DEBUG_IS_SHARED_PAGE,
				vm->vm_id, ipa, 0);

		if (ret == -EFAULT) {
			snprintf(tmp, sizeof(tmp),
				"Invalid address 0X%llx: Not mapped\n", ipa);
			goto exit;
		}

		snprintf(tmp, sizeof(tmp), "0x%llx -> 0x%llx: %s\n",
				ipa, pa, (ret ? "Shared w/ host" : "Protected"));

exit:
		if (copy_to_user(buf, tmp, sizeof(tmp)))
			return -EFAULT;

		*offset += sizeof(tmp);

		return sizeof(tmp);
	}

	return 0;
}

static ssize_t translate_addr_ops_set_ipa(struct file *file, const char __user *buf,
					size_t len, loff_t *offset)
{
	char tmp[64] = {0, };
	struct hvm *vm = file->private_data;
	ssize_t ret = -EFAULT;

	if (*offset == 0) {
		// set ipa to be translated
		if (copy_from_user(tmp, buf, sizeof(tmp)))
			return -EFAULT;

		ret = kstrtoull(tmp, 0, &vm->vm_debug.ipa_requested);
		if (ret)
			return ret;

		return len;
	}

	return 0;
}

static const struct file_operations translate_addr_ops = {
	.open = simple_open,
	.read = translate_addr_ops_get_pa,
	.write = translate_addr_ops_set_ipa,
};

int hvm_drv_debug_init(void)
{
	hvm_debugfs_dir = debugfs_create_dir("hvm", NULL);

	return 0;
}

int hvm_create_vm_debugfs(struct hvm *hvm)
{
	struct dentry *dent;
	char dname[16];

	snprintf(dname, sizeof(dname), "VM-%d", hvm->vm_id);

	dent = debugfs_create_dir(dname, hvm_debugfs_dir);

	debugfs_create_file("stage2_ipa_to_pa", 0644, dent, hvm, &translate_addr_ops);

	return 0;
}

int hvm_destroy_vm_debugfs(struct hvm *hvm)
{
	char dname[16];

	snprintf(dname, sizeof(dname), "VM-%d", hvm->vm_id);

	debugfs_lookup_and_remove(dname, hvm_debugfs_dir);

	return 0;
}

