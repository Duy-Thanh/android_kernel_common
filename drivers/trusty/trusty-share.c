// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Google, Inc.
 *
 * This trusty-driver module contains the SMC API for the trusty-driver to
 * communicate with the trusty-kernel for shared memory
 * registration/unregistration.
 */

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/trusty/trusty.h>
#include "trusty-share.h"

#define NO_ERROR 0

/**
 * struct trusty_share_info - Trusty shared resources info local to Trusty-Driver
 * @dev: ptr to the trusty-device instance
 * @debugfs_dir: used for exporting trusty-share information to debugfs
 * @debugfs_file: file in the debugfs directory that exposes the per-cpu shadow priorities
 * @shared: ptr to the shared-memory block
 * @sg: ptr to the scatter-gather list used for shared-memory buffers
 * @tp_shared_mem_id: trusty-priority shared-memory id
 * @buf_size: trusty-priority shared-memory buffer-size
 * @num_pages: number of pages containing the allocated shared-memory buffer
 */
struct trusty_share_info {
	struct device *dev;
	struct dentry *debugfs_dir;
	struct dentry *debugfs_file;
	struct scatterlist *sg;
	trusty_shared_mem_id_t shared_mem_id;
	char *vm;
	u32 mem_size;
	u32 buf_size;
	u32 num_pages;
};

static int trusty_share_debugfs_show(struct seq_file *s, void *data)
{
	struct trusty_shared *shared;
	int i;

	shared = (struct trusty_shared *)(s->private);

	for (i = 0; i < shared->cpu_count; i++) {
		struct trusty_shadow_priority *shprio;

		shprio = &shared->trusty_shadow_priority_table[i];
		seq_printf(s, "cpu[%d]: cur_priority=%d ask_priority=%d\n", i,
			   shprio->cur_shadow_priority,
			   shprio->ask_shadow_priority);
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(trusty_share_debugfs);

static void trusty_share_debugfs_init(struct trusty_share_info *info)
{
	info->debugfs_dir = debugfs_create_dir("trusty-share", NULL);
	if (!info->debugfs_dir) {
		info->debugfs_file = NULL;
		pr_warn("Error creating debugfs dir for trusty-share\n");
		return;
	}
	info->debugfs_file =
		debugfs_create_file("shadow-priority", 0444, info->debugfs_dir,
				    info->vm, &trusty_share_debugfs_fops);
	if (!info->debugfs_file) {
		debugfs_remove_recursive(info->debugfs_dir);
		info->debugfs_dir = NULL;
		pr_warn("Error creating shadow-priority file in debugfs dir for trusty-share\n");
		return;
	}
}

static void trusty_share_debugfs_fini(struct trusty_share_info *info)
{
	debugfs_remove_recursive(info->debugfs_dir);
}

static int trusty_share_resources_allocate(struct trusty_share_info *info)
{
	struct scatterlist *sg;
	unsigned char *mem;
	trusty_shared_mem_id_t mem_id;
	int result = NO_ERROR;
	int i;

	info->mem_size = sizeof(u32) +
			 nr_cpu_ids * sizeof(struct trusty_shadow_priority);
	info->num_pages = round_up(info->mem_size, PAGE_SIZE) / PAGE_SIZE;
	info->buf_size = info->num_pages * PAGE_SIZE;

	dev_info(info->dev, "*** %s: mem_size=%d,  num_pages=%d,  buf_size=%d",
		 __func__, info->mem_size, info->num_pages, info->buf_size);

	info->sg = kcalloc(info->num_pages, sizeof(*info->sg), GFP_KERNEL);
	if (!info->sg) {
		result = ENOMEM;
		dev_err(info->dev, "kcalloc() failed! error=%d\n", result);
		goto err_rsrc_alloc_sg;
	}

	mem = vzalloc(info->buf_size);
	if (!mem) {
		result = -ENOMEM;
		goto err_rsrc_alloc_mem;
	}
	info->vm = mem;
	dev_info(info->dev, "*** %s: vm=%llx  size=%d\n", __func__, info->vm,
		 info->buf_size);

	sg_init_table(info->sg, info->num_pages);
	for_each_sg(info->sg, sg, info->num_pages, i) {
		struct page *pg = vmalloc_to_page(mem + (i * PAGE_SIZE));

		if (!pg) {
			result = -ENOMEM;
			goto err_rsrc_alloc_page;
		}
		sg_set_page(sg, pg, PAGE_SIZE, 0);
	}

	result = trusty_share_memory(info->dev, &mem_id, info->sg,
				     info->num_pages, PAGE_KERNEL);
	if (result != NO_ERROR) {
		dev_err(info->dev, "trusty_share_memory failed: %d\n", result);
		goto err_rsrc_share_mem;
	}
	dev_info(info->dev, "*** %s: shared_mem_id=0x%llx", __func__, mem_id);
	info->shared_mem_id = mem_id;
	return result;

err_rsrc_share_mem:
err_rsrc_alloc_page:
	vfree(info->vm);
err_rsrc_alloc_mem:
	kfree(info->sg);
err_rsrc_alloc_sg:
	return result;
}

void *trusty_register_share(struct device *device)
{
	int result = NO_ERROR;
	struct trusty_share_info *info = NULL;
	struct trusty_shared *shared;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	info->dev = device;

	result = trusty_share_resources_allocate(info);
	if (result != NO_ERROR)
		goto err_resources_alloc;

	shared = (struct trusty_shared *)info->vm;
	shared->cpu_count = nr_cpu_ids;

	dev_info(device, "*** %s: calling api SMC_SC_SHARE_REGISTER...\n",
		 __func__);

	result = trusty_std_call32(info->dev, SMC_SC_SHARE_REGISTER,
				   (u32)info->shared_mem_id,
				   (u32)(info->shared_mem_id >> 32),
				   info->buf_size);
	if (result == SM_ERR_UNDEFINED_SMC) {
		dev_info(
			info->dev,
			"trusty-share not supported on secure side, error=%d\n",
			result);
		goto err_smc_std_call32;
	} else if (result < 0) {
		dev_err(device,
			"trusty std call32 (SMC_SC_SHARE_REGISTER) failed: %d\n",
			result);
		goto err_smc_std_call32;
	}

	dev_info(device, "*** %s: share_info=%llx\n", __func__, (u64)info);
	trusty_share_debugfs_init(info);
	return (void *)info;

err_smc_std_call32:
	kfree(info->sg);
err_resources_alloc:
	kfree(info);
	return NULL;
}

int trusty_unregister_share(void *share_state)
{
	int result;
	struct trusty_share_info *info;

	info = (struct trusty_share_info *)share_state;

	trusty_share_debugfs_fini(info);

	/* ask Trusty to release the Trusty-side resources */
	result = trusty_std_call32(info->dev, SMC_SC_SHARE_UNREGISTER,
				   (u32)info->shared_mem_id,
				   (u32)(info->shared_mem_id >> 32), 0);
	if (WARN_ON(result)) {
		dev_info(info->dev,
			 "trusty failed to release shared memory, error=%d\n",
			 result);
		dev_info(info->dev,
			 "WARNING: trusty may have leaked some resources!!\n");
	}

	kfree(info->sg);
	kfree(info);

	return result; /* if unregister failed, trusty may have leaked some resources */
}
