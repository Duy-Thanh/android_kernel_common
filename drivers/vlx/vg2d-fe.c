// SPDX-License-Identifier: GPL-2.0+
/*
 *  vg2d-fe.c - virtual G2D frontend driver
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

#include <linux/version.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/syscalls.h>
#include <nk/nkern.h>
#include "vrpc.h"
#include "vdmaheap_kapi.h"
#include <vlx/vg2d_common.h>
#include <linux/delay.h>
#include <soc/samsung/debug-snapshot.h>

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
#include "vfence2_kapi.h"
#endif

#if 0
#define VG2D_DEBUG
#endif

#ifdef VG2D_DEBUG
#define DTRACE(format, args...)                                                \
	pr_notice("VG2D-FE: %s: " format, __func__, ##args)
#else
#define DTRACE(x...)
#endif

#define TRACE(x...) pr_notice("VG2D-FE: " x)
#define WTRACE(x...) pr_warn("VG2D-FE: Warning: " x)
#define ETRACE(x...) pr_err("VG2D-FE: Error: " x)

typedef struct vg2d_t {
	struct mutex lock;
	struct vrpc_t *vrpc;
	void *vrpc_data;
	struct miscdevice misc;
	struct vdmaheap_client *vdmaheap;
	int target_dmabufs_cnt;
	struct dma_buf *target_dmabufs[G2D_MAX_BUFFERS];
	int source_dmabufs_cnt[G2D_MAX_IMAGES];
	struct dma_buf *source_dmabufs[G2D_MAX_IMAGES][G2D_MAX_BUFFERS];
#ifndef CONFIG_VLX_VG2D_DISABLE_VERSIONING
	u32 fe_version;
	u32 be_version;
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_connection *vfence2_connection;
	struct vfence2_client *vfence2_client;
	vfence2_id_t target_vfid;
	vfence2_id_t source_vfid[G2D_MAX_IMAGES];
#endif
} vg2d_t;

#ifdef VG2D_DEBUG
static const char *vg2d_cmd_name[VG2D_CMD_MAX] = VG2D_CMD_NAME;
#endif

static vg2d_t *vg2d_dev;
static vg2d_t *vfimg2d_dev;

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
    /*
     * Unexport fence fd
     */
    static void
vg2d_put_fence_vfid(vg2d_t *dev, vfence2_id_t *vfid)
{
    if (*vfid != VFENCE2_ID_NONE) {
	vfence2_unexport_fence(dev->vfence2_connection, *vfid);
	*vfid = VFENCE2_ID_NONE;
    }
}
#endif

/*----- VRPC call management routine -----*/

static vrpc_size_t vg2d_call(vg2d_t *vg2d, const vg2d_cmd_t vcmd,
			     const nku32_f arg, const vrpc_size_t size0)
{
	struct vrpc_t *vrpc = vg2d->vrpc;
	vg2d_req_t *req = vg2d->vrpc_data;
	vrpc_size_t size;
	int retry_count = 0;
	bool reopen_success = false;

	DTRACE("vcmd %s arg %x\n", vg2d_cmd_name[vcmd], arg);
	do {
		req->vcmd = vcmd;
		req->arg = arg;
		size = size0;
		if (!vrpc_call_non_interruptible(vrpc, &size)) {
			reopen_success = true;
			break;
		}
		ETRACE("Lost backend. Closing and reconnecting.\n");
		vrpc_close(vrpc);

		if (vrpc_client_open(vrpc, 0, 0)) {
			BUG();
		}
		cpu_relax();
		mdelay(VG2D_MAX_REOPEN_PERIOD_TIME_IN_MSEC);
		retry_count++;
		TRACE("Re-established frontend link.\n");
		dbg_snapshot_printk("vg2d-fe retry vcmd : %d\n", (int)vcmd);
	} while (retry_count < VG2D_MAX_RECONNECT_TRY_COUNT);

	if (!reopen_success) {
		ETRACE("vg2d-fe reconnect failed! retry cnt(%d)\n",
		       retry_count);
		dbg_snapshot_printk(
			"[VG2D-FE] %s vg2d-fe reconnect failed! retry cnt(%d)\n",
			__func__, retry_count);
		return 0;
	}

	return size;
}

/*----- G2D_IOC_PROCESS management routines -----*/

/*
     * Release and unexport layers dmabufs
     */
static void vg2d_put_buffer(vg2d_t *dev, struct g2d_layer_data *layer,
			    vdmaheap_buffer_gid_t gids[], uuid_t creds[],
			    struct dma_buf *dmabufs[], int dmabufs_cnt)
{
	unsigned int i;

	if (layer->buffer_type == G2D_BUFTYPE_DMABUF) {
		for (i = 0; i < dmabufs_cnt; i++) {
			dma_buf_put(dmabufs[i]);
			vdmaheap_kapi_unexport(dev->vdmaheap, gids[i], &creds[i]);
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
	vdmaheap_buffer_gid_t *gids = &req->target_gids[0];
	uuid_t *creds = &req->target_creds[0];
	struct dma_buf **dmabufs = &dev->target_dmabufs[0];
	int dmabufs_cnt = dev->target_dmabufs_cnt;

	vg2d_put_buffer(dev, target, gids, creds, dmabufs, dmabufs_cnt);
}

/*
     * Release sources layers dmabufs
     */
static void vg2d_put_source(vg2d_t *dev, vg2d_req_ioc_proc_t *req)
{
	struct g2d_task_data *data = &req->task_data;
	struct g2d_layer_data *source = data->source;
	vdmaheap_buffer_gid_t *gids;
	uuid_t *creds;
	struct dma_buf **dmabufs;
	int dmabufs_cnt;
	unsigned int i;

	for (i = 0; i < data->num_source; i++) {
		gids = &req->source_gids[i][0];
		creds = &req->source_creds[i][0];
		dmabufs = &dev->source_dmabufs[i][0];
		dmabufs_cnt = dev->source_dmabufs_cnt[i];
		vg2d_put_buffer(dev, &source[i], gids, creds, dmabufs, dmabufs_cnt);
	}
}

/*
     * Get and export layers dmabufs
     */
static int vg2d_get_buffer(vg2d_t *dev, struct g2d_layer_data *layer,
			   vdmaheap_buffer_gid_t gids[], uuid_t creds[],
			   struct dma_buf *dmabufs[], int *dmabufs_cnt)
{
	struct g2d_buffer_data *buffer;
	struct dma_buf *dmabuf;
	unsigned int i;
	int ret = 0;
	vdmaheap_buffer_gid_t gid;
	uuid_t cred;

	if (layer->buffer_type != G2D_BUFTYPE_DMABUF &&
	    layer->buffer_type != G2D_BUFTYPE_EMPTY) {
		ETRACE("Only G2D_BUFTYPE_DMABUF and G2D_BUFTYPE_EMPTY supported\n");
		return -ENOTSUPP;
	}

	if (layer->buffer_type == G2D_BUFTYPE_DMABUF) {
		for (i = 0; i < layer->num_buffers; i++) {
			buffer = &layer->buffer[i];
			dmabuf = dma_buf_get(buffer->dmabuf.fd);
			if (!IS_ERR(dmabuf)) {
				ret = vdmaheap_kapi_export(dev->vdmaheap, dmabuf, &gid,
						       &cred);
				if (ret) {
					ETRACE("Failed to export dma buf\n");
					dma_buf_put(dmabuf);
					break;
				}
			} else {
				ETRACE("Failed to get dmabuf from fd %d\n",
				       buffer->dmabuf.fd);
				ret = PTR_ERR(dmabuf);
				break;
			}
			if (!ret) {
				DTRACE("exported dma buffer fd %d buf %p gid %x\n",
				       buffer->dmabuf.fd, dmabuf, gid);
				gids[i] = gid;
				creds[i] = cred;
				dmabufs[i] = dmabuf;
				*dmabufs_cnt = *dmabufs_cnt + 1;
			}
		}
	} else {
		DTRACE("G2D Empty buffer for Solidcolor and Dim layer\n");
		*dmabufs_cnt = 0;
	}
	return ret;
}

/*
     * Get and export target layers dmabufs
     */
static int vg2d_get_target(vg2d_t *dev, vg2d_req_ioc_proc_t *req)
{
	struct g2d_task_data *data = &req->task_data;
	struct g2d_layer_data *target = &data->target;
	vdmaheap_buffer_gid_t *gids = &req->target_gids[0];
	uuid_t *creds = &req->target_creds[0];
	struct dma_buf **dmabufs = &dev->target_dmabufs[0];
	int *dmabufs_cnt = &dev->target_dmabufs_cnt;
	int ret = 0;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	vfence2_id_t *vfid = &dev->target_vfid;
#endif

	*dmabufs_cnt = 0;
	ret = vg2d_get_buffer(dev, target, gids, creds, dmabufs, dmabufs_cnt);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (ret == 0) {
		if (target->flags & G2D_LAYERFLAG_ACQUIRE_FENCE) {
			ret = vfence2_export_fence(dev->vfence2_connection,
						   target->fence, vfid,
						   1 /*can_block*/);
			if (*vfid == VFENCE2_ID_NONE) {
				target->flags &= ~G2D_LAYERFLAG_ACQUIRE_FENCE;
			}
			target->fence = *vfid;
		}
	}
#endif

	return ret;
}

/*
     * Get and export sources layers dmabufs
     */
static int vg2d_get_source(vg2d_t *dev, vg2d_req_ioc_proc_t *req)
{
	int ret = 0;
	struct g2d_task_data *data = &req->task_data;
	struct g2d_layer_data __user *src = data->source;
	struct g2d_layer_data *source;
	vdmaheap_buffer_gid_t *gids;
	uuid_t *creds;
	struct dma_buf **dmabufs;
	int *dmabufs_cnt;
	unsigned int i;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	vfence2_id_t *vfid;
#endif

	data->source = &req->source[0];
	for (i = 0; i < data->num_source; i++, src++) {
		source = &req->source[i];
		gids = &req->source_gids[i][0];
		creds = &req->source_creds[i][0];
		dmabufs = &dev->source_dmabufs[i][0];
		dmabufs_cnt = &dev->source_dmabufs_cnt[i];
		*dmabufs_cnt = 0;
		if (copy_from_user(source, src, sizeof(*src))) {
			ETRACE("Failed to read source image data %d/%d\n", i,
			       data->num_source);
			ret = -EFAULT;
			break;
		}
		ret = vg2d_get_buffer(dev, source, gids, creds, dmabufs,
				      dmabufs_cnt);
		if (ret) {
			break;
		}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		vfid = &dev->source_vfid[i];
		if (source->flags & G2D_LAYERFLAG_ACQUIRE_FENCE) {
			ret = vfence2_export_fence(dev->vfence2_connection,
						   source->fence, vfid,
						   1 /*can_block*/);
			if (*vfid == VFENCE2_ID_NONE) {
				source->flags &= ~G2D_LAYERFLAG_ACQUIRE_FENCE;
			}
			source->fence = *vfid;
		}
		if (ret) {
			break;
		}
#endif
	}

	return ret;
}

/*
     * Get g2d commands
     */
static int vg2d_import_commands(vg2d_req_ioc_proc_t *req)
{
	unsigned int i;
	struct g2d_task_data *data = &req->task_data;
	struct g2d_commands *cmds = &data->commands;

	if (cmds->num_extra_regs > G2D_MAX_EXTRA_REGS) {
		ETRACE("Too many coefficient registers %d\n",
		       cmds->num_extra_regs);
		return -EINVAL;
	}
	for (i = 0; i < data->num_source; i++) {
		u32 *srccmds;

		srccmds = &req->srccmds[i][0];
		if (copy_from_user(srccmds, cmds->source[i],
				   G2DSFR_SRC_FIELD_COUNT * sizeof(u32))) {
			ETRACE("Failed to get src[%d] commands\n", i);
			return -EFAULT;
		}
		cmds->source[i] = srccmds;
		DTRACE("srccmds %d %x %x %x %x\n", i, req->srccmds[i][0],
		       req->srccmds[i][1], req->srccmds[i][2],
		       req->srccmds[i][2]);
	}
	if (copy_from_user(req->extra_regs, cmds->extra,
			   cmds->num_extra_regs * sizeof(struct g2d_reg))) {
		ETRACE("Failed to get %u extra commands\n",
		       cmds->num_extra_regs);
		return -EFAULT;
	}
	cmds->extra = req->extra_regs;

	return 0;
}

/*
     * Get all g2d commands, target and sources from userland
     */
static int vg2d_get_userdata(vg2d_t *dev, vg2d_req_ioc_proc_t *req)
{
	struct g2d_task_data *data = &req->task_data;
	int ret;

	if ((data->num_source < 1) || (data->num_source > G2D_MAX_IMAGES)) {
		ETRACE("Invalid number of source images %u\n",
		       data->num_source);
		return -EINVAL;
	}
	if (data->priority > G2D_MAX_PRIORITY) {
		ETRACE("Invalid number of priority %u\n", data->priority);
		return -EINVAL;
	}
	ret = vg2d_import_commands(req);
	if (ret < 0) {
		ETRACE("Failed to get commands %d\n", ret);
		return ret;
	}
	ret = vg2d_get_source(dev, req);
	if (ret < 0) {
		ETRACE("Failed to get source %d\n", ret);
		return ret;
	}
	ret = vg2d_get_target(dev, req);
	if (ret < 0) {
		ETRACE("Failed to get target %d\n", ret);
		return ret;
	}

	return 0;
}

/*----- g2d/fimg2d device file ops -----*/

/*
     * vg2d_open implements open operation required
     * by linux semantic for character devices
     */
static int vg2d_open(struct inode *inode, struct file *filp)
{
	struct miscdevice *misc = filp->private_data;
	vg2d_t *dev;
	vg2d_res_t *res;
	vrpc_size_t size;
	int ret = 0;
#ifndef CONFIG_VLX_VG2D_DISABLE_VERSIONING
	struct pv_driver_header_v1_t *ver = NULL;
#endif

	DTRACE("\n");

	if (!strcmp(misc->name, G2D_NAME)) {
		dev = vg2d_dev;
	} else {
		dev = vfimg2d_dev;
	}
	mutex_lock(&dev->lock);
	filp->private_data = dev;

	res = dev->vrpc_data;

#ifndef CONFIG_VLX_VG2D_DISABLE_VERSIONING
	/*
	 * Set vg2d-fe pv version
	 */
	if (dev->fe_version == 0) {
		ver = &res->vg2d_ver;
		set_fe_driver_version_no_check_hdr(ver, VG2D_FE_VERSION);
	}
#endif

	size = vg2d_call(dev, VG2D_CMD_OPEN, 0, sizeof(vg2d_req_t));
	DTRACE("got: size %d res %d value %d\n", size, res->res, res->value);
	if (size != sizeof(vg2d_res_t) || res->res) {
		ret = -EFAULT;
	}
	mutex_unlock(&dev->lock);

#ifndef CONFIG_VLX_VG2D_DISABLE_VERSIONING
	if (dev->be_version == 0) {
		if (!get_be_driver_version(ver)) {
			ETRACE("be_version(%#x) is invalid\n", ver->be_version);
			goto open_failed_by_version_mismatch;
		}

		if (!is_fe_interface_compatible(ver, VG2D_FE_VERSION, VG2D_DRV_VERSION)) {
			ETRACE("major version mismatching : fe_version(%#x), be_version(%#x)\n",
				ver->fe_version, ver->be_version);
			goto open_failed_by_version_mismatch;
		} else {
			dev->fe_version = ver->fe_version;
			dev->be_version = ver->be_version;
			TRACE("%s: fe_version(%#x), be_version(%#x)\n",
				dev->misc.name, dev->fe_version, dev->be_version);
		}
	}

	return ret;
open_failed_by_version_mismatch:
	ret = -EPERM;
	size = vg2d_call(dev, VG2D_CMD_RELEASE, 0, sizeof(vg2d_req_t));
	DTRACE("got: size %d res %d value %d\n", size, res->res, res->value);
	if (size != sizeof(vg2d_res_t) || res->res) {
		ret = -EFAULT;
	}
#endif
	return ret;
}

/*
     * vg2d_release implements release (close) operation
     * required by linux semantic for character devices
     */
static int vg2d_release(struct inode *inode, struct file *filp)
{
	vg2d_t *dev;
	vg2d_res_t *res;
	vrpc_size_t size;
	int ret = 0;

	DTRACE("\n");

	dev = filp->private_data;
	mutex_lock(&dev->lock);
	res = dev->vrpc_data;
	size = vg2d_call(dev, VG2D_CMD_RELEASE, 0, sizeof(vg2d_req_t));
	DTRACE("got: size %d res %d value %d\n", size, res->res, res->value);
	if (size != sizeof(vg2d_res_t) || res->res) {
		ret = -EFAULT;
	}
	mutex_unlock(&dev->lock);

	return ret;
}

/*
     * vg2d_ioctl implements ioctl operation required
     * by linux semantic for character devices
     */
static long vg2d_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	vg2d_t *dev;
	vg2d_cmd_t vcmd;
	vg2d_res_t *res;
	vrpc_size_t size;
	int ret = 0;

	DTRACE("cmd %x arg %ld\n", cmd, arg);

	dev = filp->private_data;
	mutex_lock(&dev->lock);

	switch (cmd) {
	case G2D_IOC_PROCESS: {
		vg2d_req_ioc_proc_t *req = dev->vrpc_data;
		struct g2d_task_data *data;
		struct g2d_task_data __user *uptr;
		int i;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		int data_num_release_fences;
		s32 *data_release_fences;
		s32 release_fences[G2D_MAX_RELEASE_FENCES];
#endif

		res = dev->vrpc_data;
		dev->target_dmabufs_cnt = 0;
		for (i = 0; i < G2D_MAX_IMAGES; i++) {
			dev->source_dmabufs_cnt[i] = 0;
		}
		uptr = (struct g2d_task_data __user *)arg;
		data = &req->task_data;
		if (copy_from_user(data, uptr, sizeof(*data))) {
			ETRACE("Failed to read g2d_task_data\n");
			ret = -EFAULT;
			break;
		}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		data_num_release_fences = data->num_release_fences;
		data_release_fences = data->release_fences;
		if (data_num_release_fences > G2D_MAX_RELEASE_FENCES) {
			ETRACE("Invalid release fence count %d\n",
				data_num_release_fences);
			ret = -EINVAL;
			break;
		}
#endif
		ret = vg2d_get_userdata(dev, req);
		if (!ret) {
			vcmd = VG2D_CMD_IOC_PROCESS;
			size = sizeof(vg2d_req_ioc_proc_t);
			DTRACE("vcmd %d name %s\n", vcmd, vg2d_cmd_name[vcmd]);
			size = vg2d_call(dev, vcmd, 0, size);
			ret = res->res;
			if (ret) {
				ETRACE("Failed to process ioctl %d\n", ret);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
				vg2d_put_fence_vfid(dev, &dev->target_vfid);
				for (i = 0; i < data->num_source; i++) {
					vg2d_put_fence_vfid(dev, &dev->source_vfid[i]);
				}
#endif
				goto end_fence;
			}
			if (data->num_release_fences == 0)
				goto end_fence;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
			if (res->value == VFENCE2_ID_NONE) {
				ret = -EFAULT;
				size = 0;
				ETRACE("Failed to get fence vfid\n");
				goto end_fence;
			}
			ret = vfence2_import_fence(dev->vfence2_connection,
						   res->value, &release_fences[0]);
			if (ret) {
				size = 0;
				ETRACE("vfence2_import_fence release fence failed %d\n", ret);
			} else {
				for (i = 1; i < data_num_release_fences; i++) {
#ifndef CONFIG_ARCH_HAS_SYSCALL_WRAPPER
					release_fences[i] = sys_fcntl(release_fences[0],
								      F_DUPFD_CLOEXEC, 0);
#else
					struct file *file;

					file = fget(release_fences[0]);
					BUG_ON(!file);
					release_fences[i] = get_unused_fd_flags(O_CLOEXEC);
					get_file(file);
					fd_install(release_fences[i], file);
					fput(file);
#endif
					if (release_fences[i] < 0) {
						ret = release_fences[i];
						while (i-- > 0)
#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
							ksys_close(release_fences[i]);
#else
							sys_close(release_fences[i]);
#endif
						size = 0;
						ETRACE("Failed to duplicate fd\n");
						goto end_fence;
					}
				}
				if (copy_to_user(data_release_fences, release_fences,
						 sizeof(u32) * data_num_release_fences)) {
					while (i-- > 0)
#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
						ksys_close(release_fences[i]);
#else
						sys_close(release_fences[i]);
#endif
					ret = -EFAULT;
					size = 0;
					ETRACE("Failed to copy release fence fds to user\n");
					goto end_fence;
				}
			}
#endif
		} else {
			size = 0;
			ETRACE("vg2d_get_userdata failed\n");
		}
end_fence:
		vg2d_put_target(dev, req);
		vg2d_put_source(dev, req);

	if (!ret)
	    ret = put_user(data->laptime_in_usec, &uptr->laptime_in_usec);

		res->res = ret;
		DTRACE("got: size %d res %d value %d\n", size, res->res,
		       res->value);
		if (size != sizeof(vg2d_res_t) || res->res) {
			ret = -EFAULT;
		}
		break;
	}

	case G2D_IOC_PRIORITY: {
		vg2d_req_ioc_prio_t *req = dev->vrpc_data;
		long unsigned int *data;
		void __user *uptr;

		uptr = (void __user *)arg;
		data = &req->prio_data;
		if (copy_from_user(data, uptr, sizeof(*data))) {
			ETRACE("Failed to read g2d_priority\n");
			ret = -EFAULT;
			break;
		}
		vcmd = VG2D_CMD_IOC_PRIORITY;
		size = sizeof(vg2d_req_ioc_prio_t);
		DTRACE("vcmd %d name %s\n", vcmd, vg2d_cmd_name[vcmd]);
		size = vg2d_call(dev, vcmd, 0, size);

		res = dev->vrpc_data;
		DTRACE("got: size %d res %d value %d\n", size, res->res,
		       res->value);
		if (size != sizeof(vg2d_res_t) || res->res) {
			ret = -EFAULT;
		}
		break;
	}

	case G2D_IOC_PERFORMANCE: {
		vg2d_req_ioc_perf_t *req = dev->vrpc_data;
		struct g2d_performance_data *data;
		void __user *uptr;

		uptr = (void __user *)arg;
		data = &req->perf_data;
		if (copy_from_user(data, uptr, sizeof(*data))) {
			ETRACE("Failed to read g2d_performance\n");
			ret = -EFAULT;
			break;
		}
		vcmd = VG2D_CMD_IOC_PERFORMANCE;
		size = sizeof(vg2d_req_ioc_perf_t);
		DTRACE("vcmd %d name %s\n", vcmd, vg2d_cmd_name[vcmd]);
		size = vg2d_call(dev, vcmd, 0, size);

		res = dev->vrpc_data;
		DTRACE("got: size %d res %d value %d\n", size, res->res,
		       res->value);
		if (size != sizeof(vg2d_res_t) || res->res) {
			ret = -EFAULT;
		}
		break;
	}

	default:
		ETRACE("ioctl 0x%x not handled\n", cmd);
		ret = -ENOIOCTLCMD;
	}
	mutex_unlock(&dev->lock);

	return ret;
}

/*
     * This data structure will we passed as a parameter
     * to linux character device framework and inform it
     * we have implemented all 3 basic operations (open, release, ioctl).
     */
static const struct file_operations vg2d_fops = {
	.owner = THIS_MODULE,
	.open = vg2d_open,
	.release = vg2d_release,
	.unlocked_ioctl = vg2d_ioctl,
};

/*----- Device init and cleanup -----*/

/*
     * Virtual device cleanup routine:
     * - close the vdmaheap kapi session
     * - deregister the misc device
     * - close the vrpc session
     * - free device structure
     */
static void vg2d_dev_cleanup(vg2d_t *dev)
{
	if (dev == NULL) {
		return;
	}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (dev->vfence2_connection) {
		vfence2_disconnect(dev->vfence2_connection);
	}
	DTRACE("vfence2_disconnect\n");
	if (dev->vfence2_client) {
		vfence2_destroy_client(dev->vfence2_client);
	}
	DTRACE("vfence2_destroy_client\n");
#endif
	if (dev->vdmaheap) {
		vdmaheap_kapi_client_release(dev->vdmaheap);
	}
	if (dev->misc.name) {
		misc_deregister(&dev->misc);
	}
	if (dev->vrpc) {
		vrpc_close(dev->vrpc);
		vrpc_release(dev->vrpc);
	}
	kfree(dev);
}

/*
     * Virtual device initialzation routine:
     * - allocate device structure
     * - register the misc device
     * - open the  vdmaheap kapi session
     */
static int vg2d_dev_init(const char *name, struct vrpc_t *vrpc)
{
	vg2d_t *dev = 0;
	int diag = 0;
	struct vdmaheap_client_props props;

	dev = kzalloc(sizeof(vg2d_t), GFP_KERNEL);
	if (!dev) {
		ETRACE("%s failed: out of memory\n", __func__);
		vrpc_close(vrpc);
		vrpc_release(vrpc);
		return -ENOMEM;
	}

	mutex_init(&dev->lock);
	dev->vrpc = vrpc;
	dev->vrpc_data = vrpc_data(vrpc);
	if (vrpc_maxsize(vrpc) < sizeof(vg2d_ipc_t)) {
		ETRACE("vrpc_maxsize() too small: %d available, %ld needed\n",
		       vrpc_maxsize(vrpc), sizeof(vg2d_ipc_t));
		vg2d_dev_cleanup(dev);
		return -ENOMEM;
	}

	dev->misc.name = name;
	if (!strcmp(name, G2D_NAME)) {
		vg2d_dev = dev;
	} else {
		vfimg2d_dev = dev;
	}
	dev->misc.minor = MISC_DYNAMIC_MINOR;
	dev->misc.fops = &vg2d_fops;
	diag = misc_register(&dev->misc);
	if (diag) {
		ETRACE("Failed to register misc device for %s\n",
		       dev->misc.name);
		dev->misc.name = 0;
		vg2d_dev_cleanup(dev);
		return diag;
	}

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
	diag = vfence2_create_client(name, &dev->vfence2_client);
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
	DTRACE("vfence2_create_connect OK\n");
#endif
	DTRACE("Probe ok\n");

	return 0;
}

/*----- Entry and exit -----*/

/*
     * VRPC Call-back when the backend server is ready
     * g2d device init
     */
static void vg2d_ready(void *cookie)
{
	struct vrpc_t *vrpc;
	int diag;

	vrpc = (struct vrpc_t *)cookie;

	DTRACE("enter\n");

	diag = vg2d_dev_init(G2D_NAME, vrpc);
	if (diag) {
		ETRACE("Device %s registration failed (%d)\n", G2D_NAME, diag);
	} else {
		TRACE("Device %s registered\n", G2D_NAME);
	}
}

/*
     * VRPC Call-back when the backend server is ready
     * fimg2d device init
     */
static void vfimg2d_ready(void *cookie)
{
	struct vrpc_t *vrpc;
	int diag;

	vrpc = (struct vrpc_t *)cookie;
	diag = vg2d_dev_init(FIMG2D_NAME, vrpc);
	if (diag) {
		ETRACE("Device %s registration failed (%d)\n", FIMG2D_NAME,
		       diag);
	} else {
		TRACE("Device %s registered\n", FIMG2D_NAME);
	}
}

/*
     * Module init.
     * Lookup vrpc vlinks for virtual G2D and FIMG2D devices
     * and open the vrpc client sessions.
     */
static int __init vg2d_init(void)
{
	struct vrpc_t *vrpc;
	int diag = 0;
	int vlinks = 0;

	DTRACE("\n");

	vrpc = vrpc_client_lookup(VG2D_VRPC_NAME, 0);
	if (vrpc) {
		DTRACE("%s vrpc link detected\n", VG2D_VRPC_NAME);
		diag = vrpc_client_open(vrpc, vg2d_ready, vrpc);
		if (diag) {
			ETRACE("vrpc_client_open %s failed (%d)\n",
			       VG2D_VRPC_NAME, diag);
			vrpc_release(vrpc);
		} else {
			vlinks++;
		}
	}
	vrpc = vrpc_client_lookup(VFIMG2D_VRPC_NAME, 0);
	if (vrpc) {
		DTRACE("%s vrpc link detected\n", VFIMG2D_VRPC_NAME);
		diag = vrpc_client_open(vrpc, vfimg2d_ready, vrpc);
		if (diag) {
			ETRACE("vrpc_client_open %s failed (%d)\n",
			       VFIMG2D_VRPC_NAME, diag);
			vrpc_release(vrpc);
		} else {
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

	if (vg2d_dev) {
		vg2d_dev_cleanup(vg2d_dev);
	}
	if (vfimg2d_dev) {
		vg2d_dev_cleanup(vfimg2d_dev);
	}

	TRACE("Module unloaded.\n");
}

module_init(vg2d_init);
module_exit(vg2d_exit);

MODULE_AUTHOR("Pascal Piovesan <pascal.piovesan@harman.com>");
MODULE_DESCRIPTION("VLX G2D driver");
MODULE_LICENSE("GPL");
