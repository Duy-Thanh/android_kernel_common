// SPDX-License-Identifier: GPL-2.0+
/*
 ****************************************************************
 *
 *  Component: vbufq-fe
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
 *  Contributor(s):
 *
 *    Nicolas Lavocat (nicolas.lavocat@harman.com)
 *
 ****************************************************************
 */

#include "vbufq-fe.h"

static void vbufq_context_list_cleanup(vbufq_link_t *vbufq_link);
static void vbufq_thread_aborted_notify(vbufq_fe_t *fe);
static _Bool vbufq_link_free(vmq_link_t *link, void *cookie);
static bool vbufq_link_create_device(vmq_link_t *link, void *cookie);
static bool vbufq_link_destroy_device(vmq_link_t *link, void *cookie);
static int vbufq_create_devices(void);
static void vbufq_context_list_set_invalid(vbufq_link_t *vbufq_link);

/*----- Data -----*/
static vbufq_fe_t vbufq_fe;
static bool vbufq_link_init(vmq_link_t *link, void *cookie);

#ifdef VBUFQ_DEBUG
static int vbufq_get_ncontext(vbufq_link_t *vbufq_link);
#endif

#include "vbufq-common.c"
/**
 * @brief helper routine permits to print header of messages
 *
 * @param [in] msg - message for which header info are interesting
 * @param [in] msg_type  - 0 if a reply, 1 if a request.
 */
static void vbufq_dump_msg(vbufq_msg_t *msg, bool msg_type)
{
#ifdef VBUFQ_DEBUG
	struct vbufq_msg_header *header = &msg->header;
	struct vbufq_msg_status *status = &msg->status;

	TRACE("===== MSG_DUMP(%s) =================\n",
	      msg_type ? "REQUEST" : "RESPONSE");
	TRACE("header-cmd_type:	%d\n", header->cmd_type);
	TRACE("header-cmd:	        %u\n", header->cmd);
	TRACE("header-be_cookie:   %p\n", header->be_cookie);
	TRACE("header-fe_cookie:   %p\n", header->fe_cookie);
	TRACE("payload-ret:	%d\n", status->ret);
	TRACE("===================================\n");
#endif
}

/**
 * @brief get status from message, in order to provide it to userspace
 *
 * In case of an open operation, set  be cookie, log an error message in case
 * if status from BE in an error.
 * @param [in,out] ctx - context that issued that sent the message msg.
 * @param [in] msg - the message for which BE send a status.
 *
 * @return status sent from BE.
 */
int vbufq_update_status(vbufq_fe_context_t *ctx, struct vbufq_msg_t *msg)
{
	int ret = 0;
	enum vbufq_cmd_type cmd_type = msg->header.cmd_type;

	ret = msg->status.ret;
	if (cmd_type == VBUFQ_CMD_FILE_OPEN) {
		ctx->cookie = msg->header.be_cookie;
	}

	if (!msg->status.success) {
		const char *name = vbufq_cmd2name(msg->header.cmd);
		ETRACE("%s: error response(cmd: %s, err: %d)\n", __func__, name,
		       msg->status.ret);
		vbufq_dump_msg(msg, 0);
	}

	return ret;
}

/*----- VMQ event handlers (callbacks) -----*/

/**
 * @brief Callback triggered when a connection to a BE is ongoing.
 *
 * It is one of the VMQ callbacks, from vlx-vmq.c in the context of
 * vbufq_thread().
 *
 * @param [in] link - the link on which a BE is available for connection.
 */
static void vbufq_cb_link_on(vmq_link_t *link)
{
	vbufq_link_t *vbufq_link;

	DTRACE("link on %d\n", vmq_peer_osid(link));
	VBUFQ_ASSERT(link != NULL);

	vbufq_link = VBUFQ_LINK(link);
	VBUFQ_ASSERT(vbufq_link != NULL);

	TRACE("link on (local <-> OS %d).\n", vmq_peer_osid(link));
	vbufq_link->is_up = true;
	vbufq_link->requests_are_allowed = true;
	wake_up_interruptible(&vbufq_link->wait_queue);
}

/**
 * @brief callback triggered when a disconnection to a BE happened
 *
 * It is one of the VMQ callbacks, from vlx-vmq.c in the context of
 * vbufq_thread().
 *
 * @param link
 */
static void vbufq_cb_link_off(vmq_link_t *link)
{
	vbufq_link_t *vbufq_link;

	DTRACE("link off %d\n", vmq_peer_osid(link));

	vbufq_link = VBUFQ_LINK(link);

	if (vbufq_link->is_up) {
		vbufq_link->requests_are_allowed = false;
		vbufq_link->is_up = false;
		vipc_ctx_abort_calls(&vbufq_link->vipc_ctx);
		vbufq_context_list_set_invalid(vbufq_link);
	}
}

/**
 * @brief VMQ callback.
 *
 * notify the vbufq_thread that a sysconf event occurred.
 * @param [in] - links list of links related to the sysconf event
 */
static void vbufq_cb_sysconf_notify(vmq_links_t *links)
{
	vbufq_fe_t *fe = VBUFQ_LINKS(links);

	fe->is_sysconf = true;
	up(&fe->thread_sem);
}

/**
 * @brief VMQ callback.
 *
 * This callback is raised when a reply to a message is coming from the BE.
 * In this case, the routine vipc_ctx_process_reply unlock the vipc_ctx_call
 * routine. This permits to perform synchronous requests with VMQ.
 *
 * @param [in,out] - link on which a reply has been received.
 */
static void vbufq_cb_return_notify(vmq_link_t *link)
{
	vbufq_msg_t *msg;
	DTRACE("FE: receiving answer to request\n");

	while (!vmq_return_msg_receive(link, (void *)&(msg))) {
		if (vipc_ctx_process_reply(&VBUFQ_LINK(link)->vipc_ctx,
					   (void *)&msg->cookie)) {
			ETRACE("error during vipc_ctx_process_reply\n");
			vmq_return_msg_free(VBUFQ_LINK(link)->link, msg);
		}
	}
}

static const vmq_callbacks_t vmq_callbacks = {
	.link_on = vbufq_cb_link_on,
	.link_off = vbufq_cb_link_off,
	.sysconf_notify = vbufq_cb_sysconf_notify,
	.return_notify = vbufq_cb_return_notify,
	.get_tx_config = vbufq_cb_get_xx_config,
};

/* VMQ threads */

/**
 * @brief thread managing VMQ events.
 *
 * @param arg - usual cookie provided launching the thread. In this case, a
 * pointer to vbufq_fe
 *
 * @return 0
 */
static int vbufq_thread(void *arg)
{
	vbufq_fe_t *fe = arg;

	DTRACE("started\n");
	while (!fe->is_thread_aborted) {
		int diag = down_interruptible(&fe->thread_sem);

		(void)diag;
		DTRACE("wakeup%s%s\n",
		       fe->is_thread_aborted ? " thread-aborted" : "",
		       fe->is_sysconf ? " sysconf" : "");
		if (fe->is_thread_aborted)
			break;
		if (fe->is_sysconf) {
			fe->is_sysconf = false;
			vmq_links_sysconf(fe->links);
		}
	}
	DTRACE("exiting\n");
	return 0;
}

/**
 * @brief create a context.
 * @details
 * - create a context belonging the  vbufq_link, initializing it.
 * - add it to the context list of vbufq_link
 * @param[in] name - name of the process associated with the context
 * @param [in, out] vbufq_link - the vbuq_link to which the context will belong.
 * @remark only called by  vbufq_fe_open
 *
 * @return context ptr if OK, NULL otherwise
 */

static struct vbufq_fe_context_t *vbufq_context_create(char *name,
						       vbufq_link_t *vbufq_link)
{
	unsigned long flags;
	struct vbufq_fe_context_t *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ETRACE("no memory to create context(%s)\n", name);
		return NULL;
	}

	ctx->vbufq_link = vbufq_link;
	ctx->name = kstrdup(name, GFP_KERNEL);
	ctx->signature = ctx;
	ctx->req_cmpt = 0;
	ctx->is_valid = true;
	INIT_LIST_HEAD(&ctx->node);

	spin_lock_irqsave(&vbufq_link->slock, flags);
	list_add_tail(&ctx->node, &vbufq_link->ctx_list);
	spin_unlock_irqrestore(&vbufq_link->slock, flags);

	DTRACE("%s: name: %s\n", __func__, ctx->name);

	return ctx;
}

/**
 * @brief destroy a context
 * @details counterpart of @ref vbufq_context_create
 * @param [in] ctx - context to be destroyed
 * @param [in] locked - if locked is true, ctx will be removed from the context list
 * without spinlock protection, otherwise this protection will be performed.
 * @remark: this function is called through @ref vbufq_context_destroy_locked and
 * @ref vbufq_context_destroy macros.
 */
static void vbufq_context_destroy_internal(vbufq_fe_context_t *ctx, bool locked)
{
	unsigned long flags;
	vbufq_link_t *vbufq_link = ctx->vbufq_link;

	if (WARN_ON(!ctx)) {
		return;
	}

	if (!locked) {
		spin_lock_irqsave(&vbufq_link->slock, flags);
	}
	list_del(&ctx->node);
	if (!locked) {
		spin_unlock_irqrestore(&vbufq_link->slock, flags);
	}

	DTRACE("%s: name(%s)\n", __func__, ctx->name);

	ctx->signature = NULL;
	ctx->vbufq_link = NULL;
	ctx->cookie = NULL;
	ctx->is_valid = false;
	vbufq_kfree_and_clear(ctx->name);
	vbufq_kfree_and_clear(ctx);
}

/**
 * @brief helper permitting to get the number on context belonging to a
 * vbufq_link
 * @param [in] vbufq_link - vbufq_link fir which contexts need to be enumerated.
 *
 * @return number of contexts. If an error occurs, 0 is sent.
 */
#ifdef VBUFQ_DEBUG
static int vbufq_get_ncontext(vbufq_link_t *vbufq_link)
{
	unsigned long flags;
	vbufq_fe_context_t *ctx, *tmp;
	int ctx_nb = 0;

	if (vbufq_link == NULL) {
		ETRACE("%s: vbufq_link is null! \n", __func__);
		goto exit;
	}

	spin_lock_irqsave(&vbufq_link->slock, flags);
	if (!list_empty(&vbufq_link->ctx_list)) {
		list_for_each_entry_safe (ctx, tmp, &vbufq_link->ctx_list,
					  node) {
			ctx_nb++;
		}
	}
	spin_unlock_irqrestore(&vbufq_link->slock, flags);

exit:
	return ctx_nb;
}
#endif

/**
 * @brief destroy all contexts belonging to a vbufq_link
 *
 * contexts destruction is protected by spinlock.
 * @param [in,out] vbufq_link for which contexts must be destroyed.
 */
static void vbufq_context_list_cleanup(vbufq_link_t *vbufq_link)
{
	unsigned long flags;
	vbufq_fe_context_t *ctx, *tmp;

	spin_lock_irqsave(&vbufq_link->slock, flags);
	if (!list_empty(&vbufq_link->ctx_list)) {
		list_for_each_entry_safe (ctx, tmp, &vbufq_link->ctx_list,
					  node) {
			vbufq_context_destroy_locked(ctx);
		}
	}
	spin_unlock_irqrestore(&vbufq_link->slock, flags);
}

/**
 * @brief set invalid all contexts related to a vbufq_link
 *
 * Called in case of be disconnection the routine @ref vbufq_cb_link_off.
 * Once set invalid, contexts will not be used for further requests.
 * @param vbufq_link
 */
static void vbufq_context_list_set_invalid(vbufq_link_t *vbufq_link)
{
	unsigned long flags;
	vbufq_fe_context_t *ctx, *tmp;

	spin_lock_irqsave(&vbufq_link->slock, flags);
	if (!list_empty(&vbufq_link->ctx_list)) {
		list_for_each_entry_safe (ctx, tmp, &vbufq_link->ctx_list,
					  node) {
			if (ctx) {
				DTRACE("invalidating ctx\n");
				ctx->is_valid = false;
			}
		}
	}
	spin_unlock_irqrestore(&vbufq_link->slock, flags);
}

/**
 * @brief provide the vbufq_link related to an inode
 *
 * This method is a getter routine to the array minor2vbufq_link.
 * The minor of the device is extracted from the inode.
 * Then, vlink associated is get from the array minor2vbufq_link.
 * @param [in] inode - inode for which we want to get the related vbufq_link
 *
 * @return a pointer to the related vbufq_link, NULL in case of erroneous input.
 */
static vbufq_link_t *inode2vbufq_link(struct inode *inode)
{
	int mj = imajor(inode);
	int mn = iminor(inode);
	vbufq_link_t *vbufq_link = NULL;
	vbufq_fe_t *fe = &vbufq_fe;
	vbufq_device_t *idev = &fe->info_devices;

	if (mj != idev->dev_major || mn < 0 || mn >= idev->ndevices ||
	    mn >= idev->nregistered_dev) {
		ETRACE("No device found with minor=%d and major=%d\n", mj, mn);
		goto exit;
	}
	vbufq_link = idev->minor2vbufq_link[mn];

exit:
	return vbufq_link;
}

/**
 * @brief open file operation routine.
 *
 * - get vbufq_link from the inode
 * - create a context and add it to private file of filp, in order to share it
 *   in other fops
 * - create a message containing an open request, send it synchronously to BE
 *   and provide status to userspace.
 * @param inode - cf man of open
 * @param filp  - cf man of open
 *
 * @return cf man of open
 */
static int vbufq_fe_open(struct inode *inode, struct file *filp)
{
	vbufq_msg_t *msg;
	vbufq_fe_context_t *ctx;
	nku64_f *reply;
	int ret = 0;
	char *name = current->comm;
#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	vbufq_fe_t *fe = &vbufq_fe;
	struct pv_driver_header_v1_t *fe_ver = &fe->pv_ver;
	struct pv_driver_header_v1_t *be_ver;
#endif
	vbufq_link_t *vbufq_link = inode2vbufq_link(inode);

	VBUFQ_ASSERT(vbufq_link != NULL);
	if (vbufq_link == NULL) {
		ETRACE("%s error, vbufq is NULL\n", __func__);
		ret = -EIO;
		goto vbufq_link_is_null;
	}

	DTRACE("%s OPEN\n", name);
	if (!vbufq_link->requests_are_allowed) {
		DTRACE("%s: BE is disconnected, operation rejected\n",
		       __func__);
		ret = -EPIPE;
		goto be_disconnected;
	}

	ctx = vbufq_context_create(name, vbufq_link);
	if (IS_ERR_OR_NULL(ctx)) {
		ETRACE("%s: fail to create context\n", name);
		ret = -ENOMEM;
		goto fail_ctx_create;
	}

	ctx->req_cmpt++;

	/* Allocate new message with no data, blocking */
	ret = vmq_msg_allocate(vbufq_link->link, 0, (void *)&msg, 0);
	if (ret) {
		ETRACE("%s Failed to allocate message\n", __func__);
		goto fail_msg_allocate;
	}

	/* Prepare request message */
	msg->header.cmd_type = VBUFQ_CMD_FILE_OPEN;
	msg->header.fe_cookie = (void *)ctx;
	msg->header.msg_nb = ctx->req_cmpt;
#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	msg->header.pv_ver = *fe_ver;
#endif
	strncpy(msg->header.name, name, sizeof(msg->header.name));

	//send synchronously message
	DTRACE("sending sync request\n");
	reply = vipc_ctx_call_masked(&vbufq_link->vipc_ctx, &msg->cookie);

	/*  get status from message */
	if (reply == NULL) {
		ret = -EPIPE;
		goto vipc_ctx_call_failed;
	}
	msg = container_of(reply, vbufq_msg_t, cookie);
	ret = vbufq_update_status(ctx, msg);

	if (!msg->status.success) {
		ETRACE("error during open on BE side\n");
		goto fail_be_open;
	}

#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	be_ver = &msg->header.pv_ver;

	fe_ver->be_version = get_be_driver_version(be_ver);
	fe_ver->be_version |= PV_VERSION_VALID_CODE;

	TRACE("fe_version(%#x), be_version(%#x)", fe_ver->fe_version,
			fe_ver->be_version);

	if (fe_ver->fe_version != fe_ver->be_version) {
		ETRACE("major version mismatching");
		goto fail_be_open;
	}
#endif

	/* */
	vmq_return_msg_free(vbufq_link->link, msg);
	DTRACE(" minor = %d, ctx nb = %d\n", vbufq_link->dev_minor,
	       vbufq_get_ncontext(vbufq_link));

	filp->private_data = ctx;
	return ret;

fail_be_open:
	vmq_return_msg_free(vbufq_link->link, msg);
vipc_ctx_call_failed:
	vbufq_context_destroy(ctx);
fail_msg_allocate:
fail_ctx_create:
be_disconnected:
vbufq_link_is_null:
	return ret;
}

/**
 * @brief release file operation routine
 *
 * - create a message containing a release request, send it synchronously to BE
 * and provide return status to user space.
 * - destroy the context related to the userland process
 * @param inode cf man of release
 * @param filp cf man of release
 *
 * @return cf man of release
 */
static int vbufq_fe_release(struct inode *inode, struct file *filp)
{
	vbufq_msg_t *msg;
	nku64_f *reply;
	int ret = 0;
	vbufq_fe_context_t *ctx = filp->private_data;
	vbufq_link_t *vbufq_link = ctx->vbufq_link;

	DTRACE("%s CLOSE \n", ctx->name);
	if (!vbufq_link->requests_are_allowed) {
		/*reached if a BE disconnection occurred */
		DTRACE("%s: BE is disconnected, operation rejected\n",
		       __func__);
		/*jump to the ctx destruction. */
		goto be_disconnected;
	}

	if (!ctx->is_valid) {
		/* reached if context was created, then a be disconnection occurred,
         * and the be came back. The ctx is then no more usable, user process
         *  is expected to close the fd.
         */
		ETRACE("%s: context is not valid\n", __func__);
		/*jump to the ctx destruction. */
		goto invalid_ctx;
	}

	/* Allocate new message with no data, blocking */
	ret = vmq_msg_allocate(vbufq_link->link, 0, (void *)&msg, 0);
	if (ret) {
		ETRACE("%s Failed to allocate message\n", __func__);
		/* since a blocking vmq_msg_allocate is performed, and size of messages is
         * constant, an error raised here means that there is no be connected,
         * so it is not a big deal that the release operation is not sent to be
         * and resources can be freed
         */
		goto fail_msg_allocate;
	}

	ctx->req_cmpt++;
	/* Prepare request message */
	msg->header.cmd_type = VBUFQ_CMD_FILE_CLOSE;
	msg->header.fe_cookie = (void *)ctx;
	msg->header.be_cookie = ctx->cookie;
	msg->header.msg_nb = ctx->req_cmpt;

	//send synchronously message
	DTRACE("sending sync request\n");
	reply = vipc_ctx_call_masked(&vbufq_link->vipc_ctx, &msg->cookie);

	if (reply == NULL) {
		ret = -EPIPE;
		goto vipc_ctx_call_failed;
	}
	msg = container_of(reply, vbufq_msg_t, cookie);

	ret = vbufq_update_status(ctx, msg);

	vmq_return_msg_free(vbufq_link->link, msg);

	vbufq_context_destroy(ctx);
	DTRACE(" minor = %d, ctx nb = %d\n", vbufq_link->dev_minor,
	       vbufq_get_ncontext(vbufq_link));

	return ret;

vipc_ctx_call_failed:
fail_msg_allocate: /*not in order of declaration on purpose (cf comments)*/
invalid_ctx:
be_disconnected:
	vbufq_context_destroy(ctx);
	return ret;
};

/*
 * @brief ioctl file operation routine
 *
 * create a message containing the related ioctl request, send it synchronously
 * to the BE, then provide status and ioctl output to userspace.
 * @param filp - cf man page of ioctl
 * @param cmd - cf man page of ioctl
 * @param arg - cf man page of ioctl
 *
 * @return
 */
static long vbufq_fe_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	int ret = 0;
	vbufq_msg_t *msg;
	nku64_f *reply;
	const char *cmd_name;

	vbufq_fe_context_t *ctx = filp->private_data;
	vbufq_link_t *vbufq_link = ctx->vbufq_link;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	vfence2_id_t vfid = VFENCE2_ID_NONE;
#endif

	if (!vbufq_link->requests_are_allowed) {
		DTRACE("%s: BE is disconnected, operation rejected\n",
		       __func__);
		ret = -EPIPE;
		goto be_disconnected;
	}

	if (!ctx->is_valid) {
		ETRACE("%s: context is not valid, user should close the related fd\n",
		       __func__);
		ret = -EPIPE;
		goto invalid_ctx;
	}

	ctx->req_cmpt++;
	cmd_name = vbufq_cmd2name(cmd);
	DTRACE("%s:receiving ioctl command %s\n", __func__, cmd_name);
	DTRACE("%s IOCTL %s %d\n", ctx->name, cmd_name, ctx->req_cmpt);

	ret = vmq_msg_allocate(vbufq_link->link, 0, (void *)&msg, 0);
	if (ret) {
		ETRACE("%s Failed to allocate message\n", __func__);
		goto fail_msg_allocate;
	}
	DTRACE("after memory allocation\n");

	/* Prepare request message */
	msg->header.cmd_type = VBUFQ_CMD_IOCTL;
	msg->header.cmd = cmd;
	msg->header.fe_cookie = (void *)ctx;
	msg->header.be_cookie = ctx->cookie;
	msg->header.msg_nb = ctx->req_cmpt;

	if (_IOC_SIZE(cmd) != 0) {
		if (copy_from_user(&msg->payload, (void __user *)arg,
				   _IOC_SIZE(cmd))) {
			ETRACE("error during copy _from_user\n");
			ret = -EFAULT;
			goto fail_copy_from_user;
		}
	} else {
		DTRACE("size of the message is null\n");
	}

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	/*
	 * Samsung plans to add fence support int BQ_BUF_ENQUEUE and
	 * BQ_BUF_QCQUIRE. for now there will be no fence support in
	 * BQ_BUF_DEQUEUE and BQ_BUF_RELEASE. Note that BQ_BUF_ENQUEUE and
	 * BQ_BUF_DEQUEUE ioctls (ubm producer) are handled by vBUFQ.
	 * BQ_BUF_ACQUIRE and BQ_BUF_RELEASE ioctls (UBM consumer) are handled
	 * directly by the BUFQ driver. As a consequence and for now vBUFQ
	 * should add fence support only in BQ_BUF_ENQUEUE.
	 */
	if (cmd == BQ_BUF_ENQUEUE) {
		if (msg->payload.bufq_buf.fence_fd != -1) {
			ret = vfence2_export_fence(vbufq_link->vfence2_connection,
						   msg->payload.bufq_buf.fence_fd,
						   &vfid, true /*can_block*/);
			if (ret) {
				ETRACE("ctx %s cmd %08x (%s) vfence2_export_fence "
				       "failed (%d)\n", ctx->name, cmd, cmd_name, ret);
				goto fail_copy_from_user;
			}
			msg->payload.bufq_buf.fence_fd = vfid;
		} else {
			msg->payload.bufq_buf.fence_fd = VFENCE2_ID_NONE;
		}
	}
#endif

	//send synchronously message
	reply = vipc_ctx_call_masked(&vbufq_link->vipc_ctx, &msg->cookie);

	if (reply == NULL) {
		ret = -EPIPE;
		goto vipc_ctx_call_failed;
	}

	msg = container_of(reply, vbufq_msg_t, cookie);
	DTRACE("ANSWER RECEIVED REQ %d\n", msg->header.msg_nb);

	ret = vbufq_update_status(ctx, msg);

	/* send result to user */
	if (copy_to_user((void __user *)arg, &msg->payload, _IOC_SIZE(cmd))) {
		ETRACE("error copying data to user\n");
		ret = -EFAULT;
	}

	/* free message */
	vmq_return_msg_free(vbufq_link->link, msg);

	return ret;

fail_copy_from_user:
	vmq_return_msg_free(vbufq_link->link, msg);
vipc_ctx_call_failed:
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	/*
	 * Unexport immediately to avoid leaks in case backend
	 * forgot to import during the synchronous call.
	 */
	if (vfid != VFENCE2_ID_NONE) {
		(void) vfence2_unexport_fence(vbufq_link->vfence2_connection, vfid);
	}
#endif
fail_msg_allocate:
invalid_ctx:
be_disconnected:
	return ret;
};

static unsigned int vbufq_fe_poll(struct file *file, poll_table *wait)
{
	vbufq_link_t *vbufq_link = inode2vbufq_link(file->f_inode);
	poll_wait(file, &vbufq_link->wait_queue, wait);
	if (vbufq_link->is_up) {
		TRACE("%s: be is connected!\n", __func__);
		return POLLIN | POLLRDNORM;
	}
	return 0;
}

/**
 * @brief initialize all the VMQ boilerplate
 *
 *
 * @return 0 in case of success, an error code otherwise.
 */
static int vbufq_vmq_init(void)
{
	int ret;
	vbufq_fe_t *fe = &vbufq_fe;

	/*  Semaphore init */
	sema_init(&fe->thread_sem, 0); /* Before it is signalled */

	ret = vmq_links_init_ex(&fe->links, "bufq", &vmq_callbacks, NULL,
				&vbufq_rx_config_be2fe, fe, true);

	if (ret) {
		ETRACE("%s cannot initialize VMQ vlinks err=%d\n", __func__,
		       ret);
		return ret;
	}

	ret = vmq_links_iterate(fe->links, vbufq_link_init, fe);
	if (ret) {
		ETRACE("%s VMQ link init failed\n", __func__);
		ret = -EIO;
		goto vlink_init_failed;
	}

	ret = vmq_links_start(fe->links);
	if (ret) {
		ETRACE("%s VMQ link start failed\n", __func__);
		ret = -EIO;
		goto vmq_links_start_failed;
	}

	/* Initialize kthreads */
	ret = vlx_thread_start(&fe->thread_desc, vbufq_thread, fe, "vbufq-fe");
	if (ret) {
		ETRACE("thread start failure\n");
		ret = -EIO;
		goto vlx_thread_start_failed;
	}

	return ret;
vlx_thread_start_failed:
vmq_links_start_failed:
	vmq_links_stop(fe->links);
vlink_init_failed:
	vmq_links_iterate(fe->links, vbufq_link_free, fe);
	vmq_links_finish(fe->links);
	return ret;
}

/**
 * @brief Cleanup all vpq related resources.
 * Counterpart of the routine @ref vbufq_vmq_init
 */
static void vbufq_vmq_cleanup(void)
{
	vbufq_fe_t *fe = &vbufq_fe;

	DTRACE("entering exit\n");
	if (fe->links) {
		vmq_links_abort(fe->links);
	}
	vbufq_thread_aborted_notify(fe);
	vlx_thread_join(&fe->thread_desc);
	DTRACE("after join\n");
	if (fe->links) {
		vmq_links_iterate(fe->links, vbufq_link_free, fe);
		vmq_links_finish(fe->links);
		fe->links = NULL;
	}
	DTRACE("vbufq_link freed\n");
}

static const struct file_operations vbufq_fe_fops = {
	.owner = THIS_MODULE,
	.open = vbufq_fe_open,
	.release = vbufq_fe_release,
	.unlocked_ioctl = vbufq_fe_ioctl,
	.poll = vbufq_fe_poll,
};

/**
 * @brief Clean up data related to devices creation
 *
 * This is the counterpart routine of @ref vbufq_init_devices
 * must be called only when all devices related to the class have been destroyed.
 */
static void vbufq_cleanup_devices(void)
{
	vbufq_fe_t *fe = &vbufq_fe;
	vbufq_device_t *idev = &fe->info_devices;

	vbufq_kfree_and_clear(idev->minor2vbufq_link);
	DTRACE("vbufq_link table freed\n");

	if (idev->class != NULL) {
		class_destroy(idev->class);
	}
	DTRACE("class destroyed\n");
	if (idev->dev_major) {
		unregister_chrdev(idev->dev_major, "vbufq_fe");
	}
	DTRACE("chrdev unregistered\n");
}

/**
 * @brief initialize devices
 *
 * This routine is the counter part of @ref vbufq_cleanup_devices.
 * It :
 *  - register a char device (getting a major)
 *  - create a device class.
 *  - allocate memory for the array minor2vbufq_link, that will permits
 *   in @ref vbufq_fe_open to get the vbufq_link related to a minor
 *  - after this init for each link init, (i.e call of the routine
 *   @ref vbufq_link_init), a device will be created.
 *   @remark: the multiple device version will be for UBM2.0 for the moment
 *   @remark in order to be able to test BE /FE in the same VM, the name
 *   of the created devices in not different of the bufq interface.
 *
 * @return 0 if OK, an error code otherwise
 */
static int vbufq_init_devices(void)
{
	int ret = 0;
	vbufq_fe_t *fe = &vbufq_fe;
	vbufq_device_t *idev = &fe->info_devices;

	DTRACE("entering in vbufq_init_devices\n ");

	idev->dev_major = register_chrdev(0, "vbufq_fe", &vbufq_fe_fops);
	if (idev->dev_major <= 0) {
		ETRACE("can't register chrdev %d\n", idev->dev_major);
		ret = idev->dev_major < 0 ? idev->dev_major : -ENXIO;
		goto failed_register_chrdev;
	}
	TRACE("%s: major = %d\n", __func__, idev->dev_major);

	idev->class = class_create(THIS_MODULE, "vbufq_fe");
	if (IS_ERR(idev->class)) {
		ret = PTR_ERR(idev->class);
		idev->class = NULL;
		ETRACE("can't create class\n");
		goto class_creation_failed;
	}

	return ret;

class_creation_failed:
	if (idev->dev_major) {
		unregister_chrdev(idev->dev_major, "vbufq_fe");
	}

failed_register_chrdev:
	return ret;
}

/**
 * @brief init module routine
 * - init devices
 * - init all VMQ boilerplate
 * @return 0 if OK, an error code otherwise
 */
static int __init vbufq_fe_init(void)
{
	int ret = 0;
	vbufq_fe_t *fe = &vbufq_fe;
	fe->info_devices.nregistered_dev = 0;

	DTRACE("starting init\n");
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	ret = vfence2_create_client(VBUFQ_DEVICE_NAME, &fe->vfence2_client);
	if (ret) {
		ETRACE("vfence2_create_client() failed (%d)\n", ret);
		goto exit;
	}
#endif
	ret = vbufq_init_devices();

	if (ret) {
		ETRACE("error during vbufq_init_devices(), leaving\n");
		goto init_devices_failed;
	}

	ret = vbufq_vmq_init();
	if (ret) {
		ETRACE("error during vbufq_vmq_init(), leaving\n");
		goto vmq_init_failed;
	}

	ret = vbufq_create_devices();
	if (ret) {
		ETRACE("%s VMQ vbufq device creation failed\n", __func__);
		goto device_creation_failed;
	}

#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	/*
	 * Set frontend driver version
	 */
	set_fe_driver_version_no_check_hdr(&fe->pv_ver, VBUFQ_PV_VERSION);
#endif

	return ret;
device_creation_failed:
	vbufq_vmq_cleanup();
vmq_init_failed:
	vbufq_cleanup_devices();
init_devices_failed:
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (fe->vfence2_client) {
		int ret2 = vfence2_destroy_client(fe->vfence2_client);
		if (ret2) {
			WTRACE("vfence2_destroy_client() failed (%d)\n", ret2);
		}
		fe->vfence2_client = NULL;
	}
exit:
#endif
	return ret;
}

/**
 * @brief notify the vbufq_thread that it need to abort
 *
 * called in the @ref vbufq_fe_exit routine
 * @param fe
 */
static void vbufq_thread_aborted_notify(vbufq_fe_t *fe)
{
	fe->is_thread_aborted = true;
	up(&fe->thread_sem);
}

/**
 * @brief routine permitting to release all resources related a link.
 * a link being associated with a vbufq_link, the following resources are released:
 * - the device
 * - the context list
 *
 * @param [in] link - link to be cleaned
 * @param [in] cookie - unused
 *
 * @return always false
 */
static _Bool vbufq_link_free(vmq_link_t *link, void *cookie)
{
	vbufq_link_t *vbufq_link = VBUFQ_LINK(link);

	DTRACE("link free (local <-> OS %d).\n", vmq_peer_osid(link));
	/* Check if ever initialized */
	if (!vbufq_link)
		return false;
	TRACE("freeing a link\n");

	vbufq_context_list_cleanup(vbufq_link);
	DTRACE("device destroyed\n");
	vbufq_kfree_and_clear(vbufq_link->device_name);

	DTRACE("ctx nb = %d\n", vbufq_get_ncontext(vbufq_link));
	vbufq_kfree_and_clear(vbufq_link);
	DTRACE("vbufq_kfree_and_clear\n");
	VBUFQ_LINK(link) = NULL;

	return false;
}

/**
 * @brief exit module routine
 *
 * - stop vbufq_thread
 * - clean all links
 * - clean devices
 */
static void vbufq_fe_exit(void)
{
	vbufq_fe_t *fe = &vbufq_fe;

	if (fe->links) {
		vmq_links_iterate(fe->links, vbufq_link_destroy_device, fe);
	}
	vbufq_vmq_cleanup();
	vbufq_cleanup_devices();
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (fe->vfence2_client) {
		const int ret = vfence2_destroy_client(fe->vfence2_client);

		if (ret) {
			WTRACE("vfence2_destroy_client() failed (%d)\n", ret);
		}
		fe->vfence2_client = NULL;
	}
#endif
}

/**
 * @brief initialize all data related to a vbufq_link
 *
 *
 * @param link
 * @param cookie
 *
 * @return
 */
static bool vbufq_link_init(vmq_link_t *link, void *cookie)
{
	bool ret = false;
	vbufq_fe_t *fe = &vbufq_fe;
	vbufq_device_t *idev = &fe->info_devices;
	vbufq_link_t *vbufq;

	VBUFQ_ASSERT(link != NULL);
	vbufq = VBUFQ_LINK(link);

	DTRACE("init of a link");
	VBUFQ_ASSERT(vbufq != NULL);

	vbufq->link = link;
	vipc_ctx_init(&vbufq->vipc_ctx, link);
	spin_lock_init(&vbufq->slock);
	INIT_LIST_HEAD(&vbufq->ctx_list);
	init_waitqueue_head(&vbufq->wait_queue);

	//increment number of detected devices.
	idev->ndevices++;

	return ret;
}

/**
 * @brief create the node related to the link
 *
 * @param [in] link - link for which a node
 * @param [in] cookie - cookie provided to the routine by the routine "vmq_links_iterate"
 *
 * @return false if OK, true otherwise
 */
static bool vbufq_link_create_device(vmq_link_t *link, void *cookie)
{
	bool ret = false;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	int res;
#endif
	vbufq_link_t *vbufq;
	vbufq_fe_t *fe = cookie;
	vbufq_device_t *idev = &fe->info_devices;
	struct device *cls_dev;
	VBUFQ_ASSERT(link != NULL);
	vbufq = VBUFQ_LINK(link);

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	res = vfence2_connect(fe->vfence2_client, vmq_peer_osid(link),
			      &vbufq->vfence2_connection);
	if (res) {
		ETRACE("vfrence2_connect(%d) failed (%d)\n", vmq_peer_osid(link), res);
		return true;	/* Stop iterating */
	}
#endif

	DTRACE("device_create: major = %d, nregistered_dev = %d\n",
	       idev->dev_major, idev->nregistered_dev);
	cls_dev =
		device_create(idev->class, NULL,
			      MKDEV(idev->dev_major, idev->nregistered_dev),
			      NULL, vbufq->device_name, idev->nregistered_dev);

	if (IS_ERR(cls_dev)) {
		ret = true;
		ETRACE("class device create failed err =%ld\n",
		       PTR_ERR(cls_dev));
	} else {
		vbufq->dev_minor = idev->nregistered_dev;
		idev->minor2vbufq_link[vbufq->dev_minor] = vbufq;
		idev->nregistered_dev++;
	}
	return ret;
}

/**
 * @brief instantiate a device for each link, using @ref vbufq_link_create_device
 *
 * @return  0, an error code otherwise
 */
static int vbufq_create_devices(void)
{
	int ret;
	vbufq_fe_t *fe = &vbufq_fe;
	vbufq_device_t *idev = &fe->info_devices;

	if (idev->ndevices <= 0) {
		ETRACE("Invalid value of vbufq_ndevices: %d\n", idev->ndevices);
		ret = -EINVAL;
		goto invalid_ndevice;
	}

	TRACE("creating %d devices\n", idev->ndevices);
	idev->minor2vbufq_link = kzalloc(
		sizeof(struct vbufq_link_t *) * idev->ndevices, GFP_KERNEL);

	if (idev->minor2vbufq_link == NULL) {
		ETRACE("cannot allocate memory for minor2vbufq_link\n");
		ret = -ENOMEM;
		goto failed_minor2vbufq_link_allocation;
	}

	ret = vmq_links_iterate(fe->links, vbufq_link_create_device, fe);
	if (ret) {
		ETRACE("error, link device creation failed");
		goto vbufq_link_create_device_failed;
	}
	return ret;
vbufq_link_create_device_failed:
	vbufq_kfree_and_clear(idev->minor2vbufq_link);
failed_minor2vbufq_link_allocation:
invalid_ndevice:
	return ret;
}

/**
 * @brief destroy the device instantiated for link
 *
 * counterpart of the method @ref vbufq_link_create_device
 *
 * @param [in] link - link for which the device has to be destroyed
 * @param cookie
 *
 * @return false
 */
static bool vbufq_link_destroy_device(vmq_link_t *link, void *cookie)
{
	vbufq_fe_t *fe = &vbufq_fe;
	vbufq_device_t *idev = &fe->info_devices;
	vbufq_link_t *vbufq_link = VBUFQ_LINK(link);

	VBUFQ_ASSERT(vbufq_link != NULL);
	TRACE("destroying device %d %d \n", idev->dev_major,
	      vbufq_link->dev_minor);
	device_destroy(idev->class,
		       MKDEV(idev->dev_major, vbufq_link->dev_minor));

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	/* If vfence2_connect() fails, we come here with NULL pointers */
	if (vbufq_link->vfence2_connection) {
		(void) vfence2_disconnect(vbufq_link->vfence2_connection);
		vbufq_link->vfence2_connection = NULL;
	}
#endif

	return false;
}

module_init(vbufq_fe_init);
module_exit(vbufq_fe_exit);

/*----- Module description -----*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Lavocat <nicolas.lavocat@harman.com>");
MODULE_DESCRIPTION("Vbufq FE driver");
/*----- End of file -----*/
