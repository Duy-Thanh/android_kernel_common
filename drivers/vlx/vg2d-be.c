// SPDX-License-Identifier: GPL-2.0+
/*
 *  vg2d-be.c - virtual G2D backend driver
 *
 *  Copyright (c) 1999-2020 Red Bend. All Rights Reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 *  You should have received a copy of the GNU General Public License Version 2
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *  Author(s):
 *    Pascal Piovesan (pascal.piovesan@harman.com)
 */

#include <linux/device.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/syscalls.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <nk/nkern.h>
#include "vrpc.h"
#include "vdmaheap_kapi.h"
#include <vlx/vg2d_common.h>
#include <soc/samsung/debug-snapshot.h>

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
#include "vfence2_kapi.h"
#endif

#define VG2D_DRIVER	"vg2d-be"

#if 0
#define VG2D_DEBUG
#endif

#ifdef VG2D_DEBUG
#define DTRACE(format, args...)                                                \
	pr_notice("VG2D-BE: %s: " format, __func__, ##args)
#else
#define DTRACE(x...)
#endif

#define TRACE(x...) pr_notice("VG2D-BE: " x)
#define WTRACE(x...) pr_warn("VG2D-BE: Warning: " x)
#define ETRACE(x...) pr_err("VG2D-BE: Error: " x)

typedef struct vg2d_t {
	struct vrpc_t *vrpc;
	void *vrpc_data;
	vrpc_size_t vrpc_maxsize;
	const char *name;
	struct vg2d_t *next;
	struct file *file;
	int file_cnt;
	struct vdmaheap_client *vdmaheap;
	int target_dmabufs_cnt;
	struct dma_buf *target_dmabufs[G2D_MAX_BUFFERS];
	int source_dmabufs_cnt[G2D_MAX_IMAGES];
	struct dma_buf *source_dmabufs[G2D_MAX_IMAGES][G2D_MAX_BUFFERS];
	vg2d_req_ioc_proc_t ioc_req;
#ifndef CONFIG_VLX_VG2D_DISABLE_VERSIONING
	u32 fe_version;
	u32 be_version;
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	int target_fence;
	int source_fence[G2D_MAX_IMAGES];
	struct vfence2_connection *vfence2_connection;
	struct vfence2_client     *vfence2_client;
#endif
} vg2d_t;

#ifdef VG2D_DEBUG
static const char *vg2d_cmd_name[VG2D_CMD_MAX] = VG2D_CMD_NAME;
#endif

static vg2d_t *vg2ds;

static vrpc_size_t vg2d_ioctl_calls(void *cookie, vrpc_size_t size);

/*----- G2D_IOC_PROCESS management routines -----*/

static void vg2d_put_buffer(struct g2d_layer_data *layer,
			    struct dma_buf *dmabufs[], int dmabufs_cnt)
{
	unsigned int i;

	/*
     * Release and close layers dmabufs
     */

	if (layer->buffer_type == G2D_BUFTYPE_DMABUF) {
		for (i = 0; i < dmabufs_cnt; i++) {
			struct g2d_buffer_data *buffer;

			buffer = &layer->buffer[i];
#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
			ksys_close(buffer->dmabuf.fd);
#else
			sys_close (buffer->dmabuf.fd);
#endif
		}
	}
}

/*
     * Release target layers dmabufs
     */
static void vg2d_put_target(vg2d_t *dev, vg2d_req_ioc_proc_t *req)
{
	struct g2d_task_data *data = &req->task_data;
	struct g2d_layer_data *target = &data->target;
	struct dma_buf **dmabufs = &dev->target_dmabufs[0];
	int dmabufs_cnt = dev->target_dmabufs_cnt;

	vg2d_put_buffer(target, dmabufs, dmabufs_cnt);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (dev->target_fence >= 0)
#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
		ksys_close(dev->target_fence);
#else
		sys_close(dev->target_fence);
#endif
#endif
}

/*
     * Release sources layers dmabufs
     */
static void vg2d_put_source(vg2d_t *dev, vg2d_req_ioc_proc_t *req)
{
	unsigned int i;
	struct g2d_task_data *data = &req->task_data;
	struct g2d_layer_data *source = data->source;

	for (i = 0; i < data->num_source; i++) {
		struct dma_buf **dmabufs;
		int dmabufs_cnt;

		dmabufs = &dev->source_dmabufs[i][0];
		dmabufs_cnt = dev->source_dmabufs_cnt[i];
		vg2d_put_buffer(&source[i], dmabufs, dmabufs_cnt);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		if (dev->source_fence[i] >= 0)
#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
			ksys_close(dev->source_fence[i]);
#else
			sys_close(dev->source_fence[i]);
#endif
#endif
	}
}

#define VG2D_MAX_IMPORTS ((G2D_MAX_IMAGES + 1) * G2D_MAX_BUFFERS)

/*
     * Fills in both gids and creds arrays.
     */
static inline void
vg2d_get_buffer_info(struct g2d_layer_data *layer, vdmaheap_buffer_gid_t src_gids[],
		     uuid_t src_creds[], vdmaheap_buffer_gid_t dst_gids[],
		     uuid_t *dst_creds[], unsigned int *count)
{
	unsigned int i;

	if (layer->buffer_type == G2D_BUFTYPE_DMABUF) {
		for (i = 0; i < layer->num_buffers; i++) {
			BUG_ON(*count >= VG2D_MAX_IMPORTS ||
			       i >= G2D_MAX_BUFFERS);

			dst_gids[*count] = src_gids[i];
			dst_creds[*count] = &src_creds[i];

			*count = *count + 1;
		}
	}
}

/*
     * Stores the dmabuf object and retrieves a file descriptor.
     */
static inline int vg2d_get_buffer_fd(struct g2d_layer_data *layer,
				     struct dma_buf *src_dmabufs[],
				     unsigned int *src_count,
				     struct dma_buf *dst_dmabufs[],
				     int *dst_count)
{
	unsigned int i;
	struct g2d_buffer_data *buffer;
	int ret;

	if (layer->buffer_type == G2D_BUFTYPE_DMABUF) {
		for (i = 0; i < layer->num_buffers; i++) {
			BUG_ON(*src_count >= VG2D_MAX_IMPORTS ||
			       i >= G2D_MAX_BUFFERS);

			buffer = &layer->buffer[i];
			dst_dmabufs[i] = src_dmabufs[*src_count];

			ret = dma_buf_fd(dst_dmabufs[i], O_CLOEXEC);
			if (ret < 0) {
				ETRACE("Failed to get fd (%d)\n", ret);
				return ret;
			}

			buffer->dmabuf.fd = ret;
			DTRACE("imported dma buffer fd %d buf %p\n",
			       buffer->dmabuf.fd, dst_dmabufs[i]);

			*dst_count = *dst_count + 1;
			*src_count = *src_count + 1;
		}
	}

	return 0;
}

/*
     * Imports all the vdmaheap buffers with one call to vdmaheap_kapi_import_multiple.
     */
static int vg2d_get_buffers(vg2d_t *dev, vg2d_req_ioc_proc_t *req)
{
	struct g2d_task_data *data = &req->task_data;
	unsigned int count = 0;
	unsigned int old_count;
	int ret;
	unsigned int i;
	vdmaheap_buffer_gid_t gids[VG2D_MAX_IMPORTS];
	uuid_t *creds[VG2D_MAX_IMPORTS];
	struct dma_buf *dmabufs[VG2D_MAX_IMPORTS];

	memset(dmabufs, 0, sizeof(*dmabufs));

	/*
     * Prepare the call to vdmaheap_kapi_import_multiple: fill in the gids and
     * creds arrays.
     */
	vg2d_get_buffer_info(&data->target, &req->target_gids[0],
			     &req->target_creds[0], gids, creds, &count);

	for (i = 0; i < data->num_source; i++) {
		vg2d_get_buffer_info(&req->source[i], &req->source_gids[i][0],
				     &req->source_creds[i][0], gids, creds,
				     &count);
	}

	ret = vdmaheap_kapi_import_multiple(dev->vdmaheap, count, gids,
					(const uuid_t **)creds, dmabufs);
	if (ret) {
		ETRACE("vdmaheap_kapi_import_multiple failed (%d)\n", ret);

		for (i = 0; i < count; i++) {
			if (!IS_ERR(dmabufs[i])) {
				BUG_ON(!dmabufs[i]);
				dma_buf_put(dmabufs[i]);
			}
		}

		return ret;
	}

	/*
	 * Store the dmabuf object and retrieve a file descriptor for
	 * all buffers.
	 */

	old_count = count;
	count = 0;

	ret = vg2d_get_buffer_fd(&data->target, dmabufs, &count,
				 &dev->target_dmabufs[0],
				 &dev->target_dmabufs_cnt);
	if (ret) {
		goto cleanup;
	}

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (data->target.flags & G2D_LAYERFLAG_ACQUIRE_FENCE) {
		ret = vfence2_import_fence(dev->vfence2_connection,
					   data->target.fence,
					   &dev->target_fence);
		if (ret) {
			ETRACE("vfence2_import_fence target failed %d\n", ret);
			goto cleanup;
		}
		data->target.fence = dev->target_fence;
	}
#endif
	for (i = 0; i < data->num_source; i++) {
		ret = vg2d_get_buffer_fd(&req->source[i], dmabufs, &count,
					 &dev->source_dmabufs[i][0],
					 &dev->source_dmabufs_cnt[i]);
		if (ret) {
			goto cleanup;
		}

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		if (data->source[i].flags & G2D_LAYERFLAG_ACQUIRE_FENCE) {
			ret = vfence2_import_fence(dev->vfence2_connection,
						   data->source[i].fence,
						   &dev->source_fence[i]);
			if (ret) {
				ETRACE("vfence2_import_fence source failed "
					"%d\n", ret);
				goto cleanup;
			}
			data->source[i].fence = dev->source_fence[i];
		}
#endif

	}

	return 0;

cleanup:
	for (i = count; i < old_count; i++) {
		dma_buf_put(dmabufs[i]);
	}

	return ret;
}

static int vg2d_open(vg2d_t *dev)
{
	struct file *file;
	char filename[16];

	if (!dev->file_cnt) {
		snprintf(filename, sizeof(filename), "/dev/%s", dev->name);
		file = filp_open(filename, O_RDWR, 0);
		if (IS_ERR(file)) {
			ETRACE("filp_open(%s) failed\n", filename);
			return PTR_ERR(file);
		} else {
			dev->file = file;
		}
	}
	dev->file_cnt++;
	return 0;
}

/*----- Remote ioctl management routines -----*/

static vrpc_size_t vg2d_cmd_ioc_process(vg2d_t *dev)
{
	vg2d_req_ioc_proc_t *req = &dev->ioc_req;
	vg2d_res_t *res = (vg2d_res_t *)dev->vrpc_data;
	int ret;
	vrpc_size_t size;
	int i;
	int retry_count = 0;
	bool reopen_success = false;
	struct g2d_task_data *data = &req->task_data;

	memcpy(req, dev->vrpc_data, sizeof(vg2d_req_ioc_proc_t));
	data->source = req->source;
	if (data->source == NULL) {
		res->res = -EINVAL;
		ETRACE("vg2d_cmd_ioc_process failed: task_data.source NULL\n");
		return 0;
	}
	if ((data->num_source < 1) ||
	    (data->num_source > G2D_MAX_IMAGES)) {
		res->res = -EINVAL;
		ETRACE("vg2d_cmd_ioc_process failed: invalid task_data.num_source\n");
		return 0;
	}

	dev->target_dmabufs_cnt = 0;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	dev->target_fence = -1;
#endif
	for (i = 0; i < G2D_MAX_IMAGES; i++) {
		dev->source_dmabufs_cnt[i] = 0;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		dev->source_fence[i] = -1;
#endif
	}

	ret = vg2d_get_buffers(dev, req);

	if (!dev->file) {
		do {
			if (vg2d_open(dev) != 0) {
				retry_count++;
				mdelay(VG2D_MAX_REOPEN_PERIOD_TIME_IN_MSEC);
			} else {
				TRACE("vg2d-be reopen success! Recover established! retry cnt(%d)\n",
				      retry_count);
				reopen_success = true;
				dbg_snapshot_printk(
					"[VG2D-BE] %s vg2d-be reopen success!\
					Recover established! retry cnt(%d)\n",
					__func__, retry_count);
				break;
			}
		} while (retry_count < VG2D_MAX_REOPEN_TRY_COUNT);

		if (!reopen_success) {
			ETRACE("vg2d-be reopen failed! retry cnt(%d)\n",
			       retry_count);
			dbg_snapshot_printk(
				"[VG2D-BE] %s vg2d-be reopen failed! retry cnt(%d)\n",
				__func__, retry_count);
			vg2d_put_target(dev, req);
			vg2d_put_source(dev, req);

			return 0;
		}
	}

	/*
	 * if BE sudden reset but FE keep using G2D, make sure that
	 * dev reopen before access the dev->file->f_op.
	 */
	smp_wmb();

	if (!ret) {
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		int release_fence = -1;
		vfence2_id_t release_vfid;

		if (data->num_release_fences != 0) {
			data->num_release_fences = 1;
			data->release_fences = &release_fence;
		} else {
			data->release_fences = NULL;
		}
#endif
		ret = dev->file->f_op->unlocked_ioctl(
			dev->file, G2D_IOC_PROCESS,
			(long unsigned int)&req->task_data);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		res->value = VFENCE2_ID_NONE;
		if (data->num_release_fences != 0 && release_fence >= 0) {
			if (!ret) {
				ret = vfence2_export_fence(dev->vfence2_connection,
							   release_fence, &release_vfid,
							   false /*no blocking*/);
				if (!ret) {
					res->value = release_vfid;
				} else {
					ETRACE("vfence2_export_fence failed\n");
				}
			}
#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
			ksys_close(release_fence);
#else
			sys_close(release_fence);
#endif
		}
#endif
	}

	vg2d_put_target(dev, req);
	vg2d_put_source(dev, req);

	if (ret) {
		res->res = ret;
		return 0;
	}

	res->res = 0;
	size = sizeof *res;

	return size;
}

static vrpc_size_t vg2d_cmd_ioc_priority(vg2d_t *dev)
{
	vg2d_req_ioc_prio_t *req = (vg2d_req_ioc_prio_t *)dev->vrpc_data;
	vg2d_res_t *res = (vg2d_res_t *)dev->vrpc_data;
	int ret;
	vrpc_size_t size;

	ret = dev->file->f_op->unlocked_ioctl(
		dev->file, G2D_IOC_PRIORITY,
		(long unsigned int)&req->prio_data);
	if (ret) {
		res->res = ret;
		return 0;
	}

	res->res = 0;
	size = sizeof *res;

	return size;
}

static vrpc_size_t vg2d_cmd_ioc_performance(vg2d_t *dev)
{
	vg2d_req_ioc_perf_t *req = (vg2d_req_ioc_perf_t *)dev->vrpc_data;
	vg2d_res_t *res = (vg2d_res_t *)dev->vrpc_data;
	int ret;
	vrpc_size_t size;

	ret = dev->file->f_op->unlocked_ioctl(
		dev->file, G2D_IOC_PERFORMANCE,
		(long unsigned int)&req->perf_data);
	if (ret) {
		res->res = ret;
		return 0;
	}

	res->res = 0;
	size = sizeof *res;

	return size;
}

/*----- VRPC call management routine -----*/

/*
     * Execute g2d ioctls coming from frontends.
     */
static vrpc_size_t vg2d_ioctl_calls(void *cookie, vrpc_size_t size)
{
	vg2d_t *dev = (vg2d_t *)cookie;
	vg2d_req_t *req = (vg2d_req_t *)dev->vrpc_data;
	vg2d_res_t *res = (vg2d_res_t *)dev->vrpc_data;
	vrpc_size_t ret;
#ifndef CONFIG_VLX_VG2D_DISABLE_VERSIONING
	struct pv_driver_header_v1_t *ver = NULL;
#endif

	DTRACE("\n");

	if (size < sizeof(vg2d_req_t)) {
		ETRACE("VRPC request size error (size %d req %d)\n", size,
		       (int)sizeof(vg2d_req_t));
		return 0;
	}

	DTRACE("guest=%d vcmd=%u(%s) arg=%u\n", vrpc_peer_id(dev->vrpc),
	       req->vcmd, vg2d_cmd_name[req->vcmd % VG2D_CMD_MAX], req->arg);

	switch (req->vcmd) {
		/*
     * Open the native g2d device (g2d or fimg2d)
     */
	case VG2D_CMD_OPEN:
		res->res = vg2d_open(dev);
		ret  = sizeof *res;
#ifndef CONFIG_VLX_VG2D_DISABLE_VERSIONING
		if (dev->fe_version == 0 && dev->be_version == 0) {
			/*
			 * Set vg2d-be pv version
			 */
			ver = &res->vg2d_ver;
			set_be_driver_version(ver, VG2D_BE_VERSION);
			dev->fe_version = get_fe_driver_version(ver);
			dev->fe_version |= PV_VERSION_VALID_CODE;
			dev->be_version = ver->be_version;
			TRACE("fe_version(%#x), be_version(%#x)\n",
				dev->fe_version, dev->be_version);
		}
#endif
		break;

	/*
     * Close the native g2d device (g2d or fimg2d)
     */
	case VG2D_CMD_RELEASE:
		res->res = 0;
		if (dev->file_cnt > 0) {
			dev->file_cnt--;
			if (!dev->file_cnt) {
				res->res = filp_close(dev->file, NULL);
				dev->file = NULL;
			}
		}
		ret = sizeof *res;
		break;

	/*
     * Perform a G2D_IOC_PROCESS native ioctl
     */
	case VG2D_CMD_IOC_PROCESS:
		ret = vg2d_cmd_ioc_process(dev);
		break;

	/*
     * Perform a G2D_IOC_PRIORITY native ioctl
     */
	case VG2D_CMD_IOC_PRIORITY:
		ret = vg2d_cmd_ioc_priority(dev);
		break;

		/*
     * Perform a G2D_IOC_PERFORMANCE native ioctl
     */
	case VG2D_CMD_IOC_PERFORMANCE:
		ret = vg2d_cmd_ioc_performance(dev);
		break;

	default:
		ETRACE("ioctl 0x%x not handled\n", req->vcmd);
		res->res = -ENOIOCTLCMD;
		ret = 0;
		break;
	}

	DTRACE("guest=%d ret=%d res->res=%d\n", vrpc_peer_id(dev->vrpc), ret,
	       res->res);

	return ret;
}

/*----- Device init and cleanup -----*/

/*
     * Virtual device cleanup routine.
     * - remove the virtual device from the list
     * - close the native device
     * - close the vrpc session
     * - close the vdmaheap kapi session
     * - free device structure
     */
static void vg2d_dev_cleanup(vg2d_t *dev)
{
	vg2d_t **link = &vg2ds;

	if (dev == NULL) {
		return;
	}

	DTRACE("VG2D for guest %d destroyed\n", vrpc_peer_id(dev->vrpc));

	/* Cleanup can be performed when vg2ds is still NULL */
	if (*link) {
		while (*link != dev)
			link = &(*link)->next;
		*link = dev->next;
	}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (dev->vfence2_connection) {
		vfence2_disconnect(dev->vfence2_connection);
		DTRACE("vfence2_disconnect\n");
	}
	if (dev->vfence2_client) {
		vfence2_destroy_client(dev->vfence2_client);
		DTRACE("vfence2_destroy_client\n");
	}
#endif
	if (dev->file) {
		filp_close(dev->file, NULL);
	}
	if (dev->vrpc) {
		vrpc_close(dev->vrpc);
		vrpc_release(dev->vrpc);
	}
	if (dev->vdmaheap) {
		vdmaheap_kapi_client_release(dev->vdmaheap);
	}
	kfree(dev);
}

/*
     * Virtual device initialzation routine:
     * - allocate device structure
     * - open the  vrpc session
     * - open the  vdmaheap kapi session
     * - insert it in the virtual device list
     */
static int vg2d_dev_init(const char *name, struct vrpc_t *vrpc)
{
	vg2d_t *dev = 0;
	int diag = 0;
	struct vdmaheap_client_props props;

	dev = kzalloc(sizeof(vg2d_t), GFP_KERNEL);
	if (!dev) {
		ETRACE("%s failed: out of memory\n", __func__);
		vrpc_release(vrpc);
		return -ENOMEM;
	}

	if (vrpc_maxsize(vrpc) < sizeof(vg2d_res_t)) {
		ETRACE("VRPC result size error (max %d res %d)\n",
		       vrpc_maxsize(vrpc), (int)sizeof(vg2d_res_t));
		vrpc_release(vrpc);
		kfree(dev);
		return -ENOMEM;
	}

	dev->vrpc = vrpc;
	dev->name = name;
	dev->vrpc_data = vrpc_data(vrpc);
	dev->vrpc_maxsize = vrpc_maxsize(vrpc);

	diag = vdmaheap_kapi_client_create(&dev->vdmaheap);
	if (diag) {
		ETRACE("Could not open vdmaheap_kapi client (%d)\n", diag);
		dev->vdmaheap = 0;
		vg2d_dev_cleanup(dev);
		return diag;
	}

	/*
	 * relax vdmaheap strict import semantics
	 * this is the first level of optimization.
	 * It is a prerequisite for enabling
	 * the import cache
	 */
	props.strict_import = false;
	/*
	 * import cache enabling requested (hint).
	 * Whether the cache is actually enabled
	 * depends on vdmaheap configuration.
	 */
	props.grace.requested = msecs_to_jiffies(VG2D_VDMAHEAP_GRACE_DELAY_MS);
	if (!props.grace.requested) {
		props.grace.requested = 1;
	}
	props.grace.accepted = -1;
	diag = vdmaheap_kapi_set_client_props(dev->vdmaheap, &props);
	if (diag) {
		ETRACE("Could not set vdmaheap_kapi client props (%d)\n", diag);
		vg2d_dev_cleanup(dev);
		return diag;
	}
	DTRACE("vdmaheap cache is enabled, grace delay:%d ms\n",
	       jiffies_to_msecs(props.grace.requested));

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	diag = vfence2_create_client(VG2D_DRIVER, &dev->vfence2_client);
	if (diag) {
		ETRACE("vfence2_create_client() failed (%d)\n", diag);
		vg2d_dev_cleanup(dev);
		return diag;
	}
	DTRACE("vfence2_create_client\n");
	diag = vfence2_connect(dev->vfence2_client, vrpc_peer_id(vrpc),
			       &dev->vfence2_connection);
	if (diag) {
		ETRACE("vfence2_connect(%d) failed (%d)\n", vrpc_peer_id(vrpc), diag);
		vg2d_dev_cleanup(dev);
		return diag;
	}
	DTRACE("vfence2_connect\n");
#endif

	diag = vrpc_server_open(dev->vrpc, vg2d_ioctl_calls, dev, 0);
	if (diag) {
		ETRACE("VRPC open failed (%d)\n", diag);
		vg2d_dev_cleanup(dev);
		return diag;
	}

	dev->next = vg2ds;
	vg2ds = dev;
	DTRACE("%s device created\n", dev->name);

	return diag;
}

/*----- Entry and exit -----*/

/*
     * Module init.
     * Lookup vrpc vlinks for virtual G2D and FIMG2D devices
     * from different VM vg2d frontends and call vg2d_dev_init
     * initialization for each of them.
     */
static int __init vg2d_init(void)
{
	int vlinks = 0;
	int diag = 0;
	struct vrpc_t *vrpc = 0;

	DTRACE("\n");

	while ((vrpc = vrpc_server_lookup(VG2D_VRPC_NAME, vrpc)) != 0) {
		DTRACE("%s vrpc link detected\n", VG2D_VRPC_NAME);
		diag = vg2d_dev_init(G2D_NAME, vrpc);
		if (!diag) {
			vlinks++;
		}
	}
	while ((vrpc = vrpc_server_lookup(VFIMG2D_VRPC_NAME, vrpc)) != 0) {
		DTRACE("%s vrpc link detected\n", VFIMG2D_VRPC_NAME);
		diag = vg2d_dev_init(FIMG2D_NAME, vrpc);
		if (!diag) {
			vlinks++;
		}
	}
	if (!vlinks) {
		diag = -ENODEV;
		ETRACE("No vlinks detected\n");
	} else {
		diag = 0;
		TRACE("%d vlinks detected\n", vlinks);
	}

	return diag;
}

/*
     * Module exit.
     * Free all devices ressources
     */
static void __exit vg2d_exit(void)
{
	DTRACE("\n");

	while (vg2ds) {
		vg2d_dev_cleanup(vg2ds);
	}

	TRACE("Module unloaded.\n");
}

module_init(vg2d_init);
module_exit(vg2d_exit);

MODULE_AUTHOR("Pascal Piovesan <pascal.piovesan@harman.com>");
MODULE_DESCRIPTION("VLX G2D driver");
MODULE_LICENSE("GPL");
