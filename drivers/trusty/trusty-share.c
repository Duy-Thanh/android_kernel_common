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

#define NO_ERROR (0)

#define TRUSTY_SHARE_DEBUG (1)

/**
 * struct trusty_share_state - Trusty share resources state local to Trusty-Driver
 * @dev: ptr to the trusty-device instance
 * @sg: ptr to the scatter-gather list used for shared-memory buffers
 * @shared_mem_id: trusty-priority shared-memory id
 * @vm_shared: vm ptr to the shared-memory block
 * @mem_size: size of trusty shared-memory block in bytes
 * @buf_size: page-aligned size of trusty shared-memory buffer in bytes
 * @num_pages: number of pages containing the allocated shared-memory buffer
 * @debugfs_dir: used for exposing trusty-share information to debugfs
 * @debugfs_file: file in the debugfs directory to expose the per-cpu shadow priorities
 */
struct trusty_share_state {
	struct device *dev;
	struct scatterlist *sg;
	trusty_shared_mem_id_t shared_mem_id;
	char *vm_shared;
	u32 mem_size;
	u32 buf_size;
	u32 num_pages;
#if TRUSTY_SHARE_DEBUG
	struct dentry *debugfs_dir;
	struct dentry *debugfs_file;
#endif /* TRUSTY_SHARE_DEBUG */
};

#if TRUSTY_SHARE_DEBUG

struct shprio_value {
	struct trusty_percpu_data *percpu_data;
	u32 cur_value;
	u32 ask_value;
};

static void trusty_share_debugfs_get(void *value_ptr)
{
	struct shprio_value *vptr = value_ptr;

	vptr->cur_value = vptr->percpu_data->cur_shadow_priority;
	vptr->ask_value = vptr->percpu_data->ask_shadow_priority;
}

static int trusty_share_debugfs_show(struct seq_file *s, void *data)
{
	struct trusty_shared *shared;
	unsigned char *share_ptr;
	int result;
	int i;

	shared = (struct trusty_shared *)(s->private);
	share_ptr = (unsigned char *)shared;

	for (i = 0; i < shared->cpu_count; i++) {
		struct shprio_value prio_value;

		prio_value.percpu_data =
			(struct trusty_percpu_data *)
				 (share_ptr + shared->hdr_size +
				    (i * shared->percpu_data_size));
		result = smp_call_function_single(i, trusty_share_debugfs_get,
						  (void *)&prio_value, 1);
		if (result != 0) {
			/* set a value that indicates an error in reading */
			prio_value.cur_value = TRUSTY_SHADOW_PRIORITY_HIGH + 1;
			prio_value.ask_value = TRUSTY_SHADOW_PRIORITY_HIGH + 1;
		}

		seq_printf(s, "cpu[%d]: cur_priority=%d ask_priority=%d\n", i,
			   prio_value.cur_value, prio_value.ask_value);
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(trusty_share_debugfs);

static void trusty_share_debugfs_init(struct trusty_share_state *share_state)
{
	share_state->debugfs_dir = debugfs_create_dir("trusty-share", NULL);
	if (!share_state->debugfs_dir) {
		pr_warn("Error creating debugfs dir for trusty-share\n");
		goto err_debugfs_dir;
	}
	share_state->debugfs_file = debugfs_create_file(
		"shadow-priority", 0444, share_state->debugfs_dir,
		share_state->vm_shared, &trusty_share_debugfs_fops);
	if (!share_state->debugfs_file) {
		pr_warn("Error creating shadow-priority file in debugfs dir for trusty-share\n");
		goto err_debugfs_file;
	}
	dev_info(share_state->dev,
		 "*** %s: 'debugfs_dir' and 'debugfs_file' created.\n",
		 __func__);
	return;

err_debugfs_file:
	debugfs_remove_recursive(share_state->debugfs_dir);
	share_state->debugfs_dir = NULL;
err_debugfs_dir:
	share_state->debugfs_file = NULL;
}

static void trusty_share_debugfs_fini(struct trusty_share_state *share_state)
{
	debugfs_remove_recursive(share_state->debugfs_dir);
}

#endif /* TRUSTY_SHARE_DEBUG */

static int
trusty_share_resources_allocate(struct trusty_share_state *share_state)
{
	struct scatterlist *sg;
	struct trusty_shared *shared;
	unsigned char *mem;
	trusty_shared_mem_id_t mem_id;
	int result = NO_ERROR;
	int i;

	share_state->mem_size = sizeof(struct trusty_shared) +
				nr_cpu_ids * sizeof(struct trusty_percpu_data);
	share_state->num_pages =
		round_up(share_state->mem_size, PAGE_SIZE) / PAGE_SIZE;
	share_state->buf_size = share_state->num_pages * PAGE_SIZE;

	dev_info(share_state->dev,
		 "*** %s: mem_size=%d,  num_pages=%d,  buf_size=%d", __func__,
		 share_state->mem_size, share_state->num_pages,
		 share_state->buf_size);

	share_state->sg = kcalloc(share_state->num_pages,
				  sizeof(*share_state->sg), GFP_KERNEL);
	if (!share_state->sg) {
		result = ENOMEM;
		dev_err(share_state->dev, "kcalloc() failed! error=%d\n",
			result);
		goto err_rsrc_alloc_sg;
	}

	mem = vzalloc(share_state->buf_size);
	if (!mem) {
		result = -ENOMEM;
		goto err_rsrc_alloc_mem;
	}
	share_state->vm_shared = mem;
	dev_info(share_state->dev, "*** %s: vm_shared=%llx  size=%d\n",
		 __func__, share_state->vm_shared, share_state->buf_size);

	sg_init_table(share_state->sg, share_state->num_pages);
	for_each_sg(share_state->sg, sg, share_state->num_pages, i) {
		struct page *pg = vmalloc_to_page(mem + (i * PAGE_SIZE));

		if (!pg) {
			result = -ENOMEM;
			goto err_rsrc_alloc_page;
		}
		sg_set_page(sg, pg, PAGE_SIZE, 0);
	}

	result = trusty_share_memory(share_state->dev, &mem_id, share_state->sg,
				     share_state->num_pages, PAGE_KERNEL);
	if (result != NO_ERROR) {
		dev_err(share_state->dev, "trusty_share_memory failed: %d\n",
			result);
		goto err_rsrc_share_mem;
	}
	dev_info(share_state->dev, "*** %s: shared_mem_id=0x%llx", __func__,
		 mem_id);
	share_state->shared_mem_id = mem_id;

	shared = (struct trusty_shared *)share_state->vm_shared;
	shared->hdr_size = sizeof(struct trusty_shared);
	shared->percpu_data_size = sizeof(struct trusty_percpu_data);

	return result;

err_rsrc_share_mem:
err_rsrc_alloc_page:
	vfree(share_state->vm_shared);
err_rsrc_alloc_mem:
	kfree(share_state->sg);
err_rsrc_alloc_sg:
	return result;
}

void *trusty_register_share(struct device *device)
{
	int result = NO_ERROR;
	struct trusty_share_state *share_state = NULL;
	struct trusty_shared *shared;
	uint share_state_size;

	share_state_size = sizeof(*share_state);

	share_state = kzalloc(share_state_size, GFP_KERNEL);
	share_state->dev = device;

	result = trusty_share_resources_allocate(share_state);
	if (result != NO_ERROR)
		goto err_resources_alloc;

	shared = (struct trusty_shared *)share_state->vm_shared;
	shared->cpu_count = nr_cpu_ids;

	dev_info(device, "*** %s: calling api SMC_SC_SHARE_REGISTER...\n",
		 __func__);

	result = trusty_std_call32(share_state->dev, SMC_SC_SHARE_REGISTER,
				   (u32)share_state->shared_mem_id,
				   (u32)(share_state->shared_mem_id >> 32),
				   share_state->buf_size);
	if (result == SM_ERR_UNDEFINED_SMC) {
		dev_info(
			share_state->dev,
			"trusty-share not supported on secure side, error=%d\n",
			result);
		goto err_smc_std_call32;
	} else if (result < 0) {
		dev_err(device,
			"trusty std call32 (SMC_SC_SHARE_REGISTER) failed: %d\n",
			result);
		goto err_smc_std_call32;
	}

	dev_info(device, "*** %s: share_state=%llx\n", __func__,
		 (u64)share_state);

#if TRUSTY_SHARE_DEBUG
	trusty_share_debugfs_init(share_state);
#endif /* TRUSTY_SHARE_DEBUG */

	return (void *)share_state;

err_smc_std_call32:
	result = trusty_reclaim_memory(share_state->dev,
				       share_state->shared_mem_id,
				       share_state->sg, share_state->num_pages);
	if (result != 0) {
		dev_info(share_state->dev,
			 "WARNING: trusty_relaim_memory() failed!!\n");
	}
	kfree(share_state->sg);
err_resources_alloc:
	kfree(share_state);
	return NULL;
}

int trusty_unregister_share(void *state)
{
	int result;
	struct trusty_share_state *share_state;

	share_state = (struct trusty_share_state *)state;

#if TRUSTY_SHARE_DEBUG
	trusty_share_debugfs_fini(share_state);
#endif /* TRUSTY_SHARE_DEBUG */

	/* ask Trusty to release the Trusty-side resources */
	result = trusty_std_call32(share_state->dev, SMC_SC_SHARE_UNREGISTER,
				   (u32)share_state->shared_mem_id,
				   (u32)(share_state->shared_mem_id >> 32), 0);
	if (WARN_ON(result)) {
		dev_info(share_state->dev,
			 "trusty failed to release shared memory, error=%d\n",
			 result);
		dev_info(share_state->dev,
			 "WARNING: trusty may have leaked some resources!!\n");
	}
	result = trusty_reclaim_memory(share_state->dev,
				       share_state->shared_mem_id,
				       share_state->sg, share_state->num_pages);
	if (result != 0) {
		dev_info(share_state->dev,
			 "WARNING: trusty_relaim_memory() failed!!\n");
	}

	kfree(share_state->sg);
	kfree(share_state);

	return result; /* if unregister failed, trusty may have leaked some resources */
}

static inline int map_trusty_prio_to_linux_nice(int trusty_prio)
{
	int new_nice;

	switch (trusty_prio) {
	case TRUSTY_SHADOW_PRIORITY_HIGH:
		new_nice = MIN_NICE;
		break;
	case TRUSTY_SHADOW_PRIORITY_LOW:
		new_nice = MAX_NICE;
		break;
	case TRUSTY_SHADOW_PRIORITY_NORMAL:
	default:
		new_nice = 0;
		break;
	}

	return new_nice;
}

static inline struct trusty_percpu_data *trusty_get_trusty_percpu_data(
		struct trusty_shared *tsh, int cpu_num)
{
	return (struct trusty_percpu_data *)((unsigned char *)tsh + tsh->hdr_size +
			(cpu_num * tsh->percpu_data_size));
}

int trusty_get_requested_nice(unsigned int cpu_num, struct trusty_share_state *tcpu_state)
{
	struct trusty_shared *tsh = (struct trusty_shared *)tcpu_state->vm_shared;

	return map_trusty_prio_to_linux_nice(
			trusty_get_trusty_percpu_data(tsh, cpu_num)->ask_shadow_priority);
}

void trusty_set_actual_nice(unsigned int cpu_num,
		struct trusty_share_state *tcpu_state, int act_nice)
{
	struct trusty_shared *tsh = (struct trusty_shared *)tcpu_state->vm_shared;
	int new_prio;

	if (act_nice >= map_trusty_prio_to_linux_nice(TRUSTY_SHADOW_PRIORITY_LOW))
		new_prio = TRUSTY_SHADOW_PRIORITY_LOW;
	else if (act_nice <= map_trusty_prio_to_linux_nice(TRUSTY_SHADOW_PRIORITY_HIGH))
		new_prio = TRUSTY_SHADOW_PRIORITY_HIGH;
	else
		new_prio = TRUSTY_SHADOW_PRIORITY_NORMAL;

	trusty_get_trusty_percpu_data(tsh, cpu_num)->cur_shadow_priority = new_prio;
}

