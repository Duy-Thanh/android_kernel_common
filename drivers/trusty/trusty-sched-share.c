// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 Google, Inc.
 *
 * This trusty-driver module contains the SMC API for the trusty-driver to
 * communicate with the trusty-kernel for shared memory
 * registration/unregistration.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/scatterlist.h>
#include <linux/trusty/trusty.h>
#include "trusty-sched-share.h"

#define DEBUG

#define TRUSTY_SCHED_SHARE_DEBUGFS (1)

#if TRUSTY_SCHED_SHARE_DEBUGFS
#include <linux/debugfs.h>
#endif /* TRUSTY_SCHED_SHARE_DEBUGFS */

/**
 * struct trusty_sched_share_state - Trusty share resources state local to Trusty-Driver
 * @dev: ptr to the trusty-device instance
 * @sg: ptr to the scatter-gather list used for shared-memory buffers
 * @sched_shared_mem_id: trusty-priority shared-memory id
 * @sched_shared_vm: vm ptr to the shared-memory block
 * @mem_size: size of trusty shared-memory block in bytes
 * @buf_size: page-aligned size of trusty shared-memory buffer in bytes
 * @num_pages: number of pages containing the allocated shared-memory buffer
 * @debugfs_dir: used for exposing trusty-share information to debugfs
 * @debugfs_file: file in the debugfs directory to expose the per-cpu shadow priorities
 */
struct trusty_sched_share_state {
	struct device *dev;
	struct scatterlist *sg;
	trusty_shared_mem_id_t sched_shared_mem_id;
	char *sched_shared_vm;
	u32 mem_size;
	u32 buf_size;
	u32 num_pages;
#if TRUSTY_SCHED_SHARE_DEBUGFS
	struct dentry *debugfs_dir;
	struct dentry *debugfs_file;
#endif /* TRUSTY_SCHED_SHARE_DEBUGFS */
};

#if TRUSTY_SCHED_SHARE_DEBUGFS
static void
trusty_sched_share_debugfs_init(struct trusty_sched_share_state *share_state);
static void
trusty_sched_share_debugfs_fini(struct trusty_sched_share_state *share_state);
#endif /* TRUSTY_SCHED_SHARE_DEBUGFS */

static int trusty_sched_share_resources_allocate(
	struct trusty_sched_share_state *share_state)
{
	struct scatterlist *sg;
	struct trusty_sched_shared *shared;
	unsigned char *mem;
	trusty_shared_mem_id_t mem_id;
	int result = 0;
	int i;

	share_state->mem_size = sizeof(struct trusty_sched_shared) +
				nr_cpu_ids * sizeof(struct trusty_percpu_data);
	share_state->num_pages =
		round_up(share_state->mem_size, PAGE_SIZE) / PAGE_SIZE;
	share_state->buf_size = share_state->num_pages * PAGE_SIZE;

	dev_info(share_state->dev,
		 "%s: mem_size=%d,  num_pages=%d,  buf_size=%d", __func__,
		 share_state->mem_size, share_state->num_pages,
		 share_state->buf_size);

	share_state->sg = kcalloc(share_state->num_pages,
				  sizeof(*share_state->sg), GFP_KERNEL);
	if (!share_state->sg) {
		result = ENOMEM;
		goto err_rsrc_alloc_sg;
	}

	mem = vzalloc(share_state->buf_size);
	if (!mem) {
		result = -ENOMEM;
		goto err_rsrc_alloc_mem;
	}
	share_state->sched_shared_vm = mem;
	dev_info(share_state->dev, "%s: sched_shared_vm=0x%llx  size=%d\n",
		 __func__, (unsigned long long)share_state->sched_shared_vm,
		 share_state->buf_size);

	sg_init_table(share_state->sg, share_state->num_pages);
	for_each_sg(share_state->sg, sg, share_state->num_pages, i) {
		struct page *pg = vmalloc_to_page(mem + (i * PAGE_SIZE));

		if (!pg) {
			result = -ENOMEM;
			goto err_rsrc_sg_lookup;
		}
		sg_set_page(sg, pg, PAGE_SIZE, 0);
	}

	result = trusty_share_memory(share_state->dev, &mem_id, share_state->sg,
				     share_state->num_pages, PAGE_KERNEL);
	if (result != 0) {
		dev_err(share_state->dev, "trusty_share_memory failed: %d\n",
			result);
		goto err_rsrc_share_mem;
	}
	dev_dbg(share_state->dev, "%s: sched_shared_mem_id=0x%llx", __func__,
		mem_id);
	share_state->sched_shared_mem_id = mem_id;

	shared = (struct trusty_sched_shared *)share_state->sched_shared_vm;
	shared->hdr_size = sizeof(struct trusty_sched_shared);
	shared->percpu_data_size = sizeof(struct trusty_percpu_data);

	return result;

err_rsrc_share_mem:
err_rsrc_sg_lookup:
	vfree(share_state->sched_shared_vm);
err_rsrc_alloc_mem:
	kfree(share_state->sg);
err_rsrc_alloc_sg:
	return result;
}

struct trusty_sched_share_state *
trusty_register_sched_share(struct device *device)
{
	int result = 0;
	struct trusty_sched_share_state *sched_share_state = NULL;
	struct trusty_sched_shared *shared;
	uint sched_share_state_size;

	sched_share_state_size = sizeof(*sched_share_state);

	sched_share_state = kzalloc(sched_share_state_size, GFP_KERNEL);
	if (!sched_share_state)
		goto err_sched_state_alloc;
	sched_share_state->dev = device;

	result = trusty_sched_share_resources_allocate(sched_share_state);
	if (result)
		goto err_resources_alloc;

	shared = (struct trusty_sched_shared *)
			 sched_share_state->sched_shared_vm;
	shared->cpu_count = nr_cpu_ids;

	dev_dbg(device, "%s: calling api SMC_SC_SCHED_SHARE_REGISTER...\n",
		__func__);

	result = trusty_std_call32(
		sched_share_state->dev, SMC_SC_SCHED_SHARE_REGISTER,
		(u32)sched_share_state->sched_shared_mem_id,
		(u32)(sched_share_state->sched_shared_mem_id >> 32),
		sched_share_state->buf_size);
	if (result == SM_ERR_UNDEFINED_SMC) {
		dev_warn(
			sched_share_state->dev,
			"trusty-share not supported on secure side, error=%d\n",
			result);
		goto err_smc_std_call32;
	} else if (result < 0) {
		dev_err(device,
			"trusty std call32 (SMC_SC_SCHED_SHARE_REGISTER) failed: %d\n",
			result);
		goto err_smc_std_call32;
	}
	dev_dbg(device, "%s: sched_share_state=%llx\n", __func__,
		(u64)sched_share_state);

#if TRUSTY_SCHED_SHARE_DEBUGFS
	trusty_sched_share_debugfs_init(sched_share_state);
#endif /* TRUSTY_SCHED_SHARE_DEBUGFS */

	return sched_share_state;

err_smc_std_call32:
	result = trusty_reclaim_memory(sched_share_state->dev,
				       sched_share_state->sched_shared_mem_id,
				       sched_share_state->sg,
				       sched_share_state->num_pages);
	if (result != 0) {
		dev_err(sched_share_state->dev,
			"trusty_reclaim_memory() failed: ret=%d mem_id=0x%llx\n",
			result, sched_share_state->sched_shared_mem_id);
		/*
		 * It is not safe to free this memory if trusty_reclaim_memory()
		 * failed. Leak it in that case.
		 */
		dev_err(sched_share_state->dev,
			"WARNING: leaking some allocated resources!!\n");
	} else {
		vfree(sched_share_state->sched_shared_vm);
	}
	kfree(sched_share_state->sg);
err_resources_alloc:
	kfree(sched_share_state);
	dev_warn(sched_share_state->dev,
		 "Trusty-Sched_Share API not available.\n");
err_sched_state_alloc:
	return NULL;
}

void trusty_unregister_sched_share(
	struct trusty_sched_share_state *sched_share_state)
{
	int result;

	if (!sched_share_state)
		return;

#if TRUSTY_SCHED_SHARE_DEBUGFS
	trusty_sched_share_debugfs_fini(sched_share_state);
#endif /* TRUSTY_SCHED_SHARE_DEBUGFS */

	/* ask Trusty to release the Trusty-side resources */
	result = trusty_std_call32(
		sched_share_state->dev, SMC_SC_SCHED_SHARE_UNREGISTER,
		(u32)sched_share_state->sched_shared_mem_id,
		(u32)(sched_share_state->sched_shared_mem_id >> 32), 0);
	if (result) {
		dev_err(sched_share_state->dev,
			"call SMC_SC_SCHED_SHARE_UNREGISTER failed, error=%d\n",
			result);
	}
	result = trusty_reclaim_memory(sched_share_state->dev,
				       sched_share_state->sched_shared_mem_id,
				       sched_share_state->sg,
				       sched_share_state->num_pages);
	if (result) {
		dev_err(sched_share_state->dev,
			"trusty_reclaim_memory() failed: ret=%d mem_id=0x%llx\n",
			result, sched_share_state->sched_shared_mem_id);
		/*
		 * It is not safe to free this memory if trusty_reclaim_memory()
		 * failed. Leak it in that case.
		 */
		dev_err(sched_share_state->dev,
			"WARNING: leaking some allocated resources!!\n");
	} else {
		vfree(sched_share_state->sched_shared_vm);
	}

	kfree(sched_share_state->sg);
	kfree(sched_share_state);
}

static inline int map_trusty_prio_to_linux_nice(int trusty_prio)
{
	int new_nice;

	switch (trusty_prio) {
	case TRUSTY_SHADOW_PRIORITY_HIGH:
		new_nice = LINUX_NICE_FOR_TRUSTY_PRIORITY_HIGH;
		break;
	case TRUSTY_SHADOW_PRIORITY_LOW:
		new_nice = LINUX_NICE_FOR_TRUSTY_PRIORITY_LOW;
		break;
	case TRUSTY_SHADOW_PRIORITY_NORMAL:
	default:
		new_nice = LINUX_NICE_FOR_TRUSTY_PRIORITY_NORMAL;
		break;
	}

	return new_nice;
}

static inline struct trusty_percpu_data *trusty_get_trusty_percpu_data(
		struct trusty_sched_shared *tsh, int cpu_num)
{
	return (struct trusty_percpu_data *)((unsigned char *)tsh + tsh->hdr_size +
			(cpu_num * tsh->percpu_data_size));
}

int trusty_get_requested_nice(unsigned int cpu_num, struct trusty_sched_share_state *tcpu_state)
{
	struct trusty_sched_shared *tsh = (struct trusty_sched_shared *)tcpu_state->sched_shared_vm;

	return map_trusty_prio_to_linux_nice(
			trusty_get_trusty_percpu_data(tsh, cpu_num)->ask_shadow_priority);
}

void trusty_set_actual_nice(unsigned int cpu_num,
		struct trusty_sched_share_state *tcpu_state, int act_nice)
{
	struct trusty_sched_shared *tsh = (struct trusty_sched_shared *)tcpu_state->sched_shared_vm;
	int new_prio;

	if (act_nice >= map_trusty_prio_to_linux_nice(TRUSTY_SHADOW_PRIORITY_LOW))
		new_prio = TRUSTY_SHADOW_PRIORITY_LOW;
	else if (act_nice <= map_trusty_prio_to_linux_nice(TRUSTY_SHADOW_PRIORITY_HIGH))
		new_prio = TRUSTY_SHADOW_PRIORITY_HIGH;
	else
		new_prio = TRUSTY_SHADOW_PRIORITY_NORMAL;

	trusty_get_trusty_percpu_data(tsh, cpu_num)->cur_shadow_priority = new_prio;
}

#if TRUSTY_SCHED_SHARE_DEBUGFS

struct shprio_value {
	struct trusty_percpu_data *percpu_data;
	u32 cur_value;
	u32 ask_value;
};

static void trusty_sched_share_debugfs_get(void *value_ptr)
{
	struct shprio_value *vptr = value_ptr;

	vptr->cur_value = vptr->percpu_data->cur_shadow_priority;
	vptr->ask_value = vptr->percpu_data->ask_shadow_priority;
}

static struct trusty_percpu_data *trusty_sched_share_get_percpu_share_ptr(
	struct trusty_sched_shared *sched_shared, uint32_t cpu_nr)
{
	struct trusty_percpu_data *percpu_data_ptr;
	unsigned char *tmp;

	tmp = (unsigned char *)sched_shared;
	tmp += sched_shared->hdr_size;
	tmp += cpu_nr * sched_shared->percpu_data_size;

	percpu_data_ptr = (struct trusty_percpu_data *)tmp;
	return percpu_data_ptr;
}

static int trusty_sched_share_debugfs_show(struct seq_file *s, void *data)
{
	struct trusty_sched_shared *sched_shared;
	int result;
	int i;

	sched_shared = (struct trusty_sched_shared *)(s->private);

	for (i = 0; i < sched_shared->cpu_count; i++) {
		struct shprio_value prio_value;

		prio_value.percpu_data =
			trusty_sched_share_get_percpu_share_ptr(sched_shared,
								i);
		result =
			smp_call_function_single(i,
						 trusty_sched_share_debugfs_get,
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

DEFINE_SHOW_ATTRIBUTE(trusty_sched_share_debugfs);

static void
trusty_sched_share_debugfs_init(struct trusty_sched_share_state *share_state)
{
	share_state->debugfs_dir =
		debugfs_create_dir("trusty-sched-share", NULL);
	if (!share_state->debugfs_dir) {
		pr_warn("Error creating debugfs dir for trusty-share\n");
		goto err_debugfs_dir;
	}
	share_state->debugfs_file = debugfs_create_file(
		"shadow-priority", 0444, share_state->debugfs_dir,
		share_state->sched_shared_vm, &trusty_sched_share_debugfs_fops);
	if (!share_state->debugfs_file) {
		pr_warn("Error creating shadow-priority file in debugfs dir for trusty-share\n");
		goto err_debugfs_file;
	}
	dev_info(
		share_state->dev,
		"*** %s: 'debugfs_dir' and 'debugfs_file' created, shared-mem=0x%llx\n",
		__func__, (unsigned long long)share_state->sched_shared_vm);
	return;

err_debugfs_file:
	debugfs_remove_recursive(share_state->debugfs_dir);
	share_state->debugfs_dir = NULL;
err_debugfs_dir:
	share_state->debugfs_file = NULL;
}

static void
trusty_sched_share_debugfs_fini(struct trusty_sched_share_state *share_state)
{
	debugfs_remove_recursive(share_state->debugfs_dir);
}

#endif /* TRUSTY_SCHED_SHARE_DEBUGFS */
