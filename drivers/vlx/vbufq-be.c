// SPDX-License-Identifier: GPL-2.0+
/*
 ****************************************************************
 *
 *  Component: vbufq-be
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
#include "vbufq-be.h"
#include <generated/autoconf.h>
static void vbufq_context_list_cleanup(vbufq_link_t *vbufq_link);
static void vbufq_work_cb(struct work_struct *work);
static _Bool vbufq_link_free(vmq_link_t *link, void *cookie);

/*----- Data -----*/
static vbufq_be_t vbufq_be;

#include "vbufq-common.c"

#ifdef CONFIG_HTF_FE
#define VBUFQ_BE_BUFQ_DEVICE_NAME "/dev/bufq-mocking"
#else
#define VBUFQ_BE_BUFQ_DEVICE_NAME "/dev/buffer-queue"
#endif
/**
 * @brief check if a vbufq_stack is empty
 *
 * @param vbufq_stack
 *
 * @return 1 if true, 0 otherwise
 */
static inline bool vbufq_stack_is_empty(vbufq_stack_t *vbufq_stack)
{
	return vbufq_stack->first == NULL;
}

/**
 * @brief create a stack_element, allocating/initializing it.
 *
 * @return a stack_element object, or NULL if memory allocation failed
 */
static vbufq_stack_element_t *vbufq_create_stack_element(void)
{
	vbufq_stack_element_t *stack_element = NULL;
	stack_element = kzalloc(sizeof(*stack_element), GFP_KERNEL);
	if (stack_element == NULL) {
		ETRACE("error, during memory allocation\n");
		goto exit;
	}

	INIT_WORK(&(stack_element->msg_box.work), vbufq_work_cb);

exit:
	return stack_element;
}

/**
 * @brief destroy a stack element
 *
 * @param stack_element
 */
static void vbufq_destroy_stack_element(vbufq_stack_element_t *stack_element)
{
	/* Wait for end of potential work usage before cleaning it. */
	cancel_work_sync(&stack_element->msg_box.work);
	kfree(stack_element);
}

/**
 * @brief pop a vbufq_stack element from a vbufq stack, not freeing it.
 *
 * @param stack
 *
 * @return a vbufq stack element
 */
static vbufq_stack_element_t *vbufq_pop(vbufq_stack_t *stack)
{
	vbufq_stack_element_t *stack_element = NULL;

	if (stack->first != NULL) {
		do {
			stack_element = stack->first;
		} while (!__sync_bool_compare_and_swap(
			&stack->first, stack_element, stack_element->next));

	} else {
		ETRACE("error, stack-> first is null\n");
	}
	return stack_element;
}

/**
 * @brief push a stack element into a vbufq_stack. It is assumed that the
 * stack element has been allocated/initialized
 *
 * @param [in,out] stack - destination stack
 * @param [in] stack_element - element to be added in the stack
 *
 */
static void vbufq_push(vbufq_stack_t *stack,
		       vbufq_stack_element_t *stack_element)
{
	do {
		stack_element->next =
			stack->first; // set up `stack->first` so the list will still be linked
		// correctly the instant the element is inserted
	} while (!__sync_bool_compare_and_swap(
		&stack->first, stack_element->next, stack_element));
}

/**
 * @brief get a msg_box object from a generator.
 * if no bufq_box if available, a new one will be created.
 *
 * @param [in] gen - generator that will provide the msg_box
 *
 * @return  a pointer a msg_box, NULL if an error occurs.
 */
static vbufq_msg_box_t *vbufq_get_msg_box(vbufq_msg_box_generator_t *gen)
{
	vbufq_msg_box_t *msg_box = NULL;
	vbufq_stack_element_t *stack_element = NULL;

	//if no buffer work ready to use, let's create one
	if (vbufq_stack_is_empty(&gen->ready2use)) {
		TRACE("ready2use stack empty, creating a new object, nbwork = %d\n",
		      ++(gen->nb_works));
		stack_element = vbufq_create_stack_element();
		if (stack_element == NULL) {
			ETRACE("%s, error, stack_element is null\n", __func__);
			goto exit;
		}
		vbufq_push(&gen->ready2use, stack_element);
	}

	stack_element = vbufq_pop(&gen->ready2use);
	msg_box = &stack_element->msg_box;

exit:
	return msg_box;
}

/**
 * @brief give back a msg_box to a generator
 *
 * @param [in,out] gen - generator that ill get back the msg_box
 * @param [in] msg_box - msg_box to be released
 */
static void vbufq_release_msg_box(vbufq_msg_box_generator_t *gen,
				  vbufq_msg_box_t *msg_box)
{
	vbufq_stack_element_t *stack_element =
		container_of(msg_box, vbufq_stack_element_t, msg_box);
	vbufq_push(&gen->ready2use, stack_element);
}

/**
 * @brief clean/deallocate all elements contained by the stack
 *
 * @param stack
 */
static void vbufq_flush_stack(vbufq_stack_t *stack)
{
	vbufq_stack_element_t *stack_element;
	while (!vbufq_stack_is_empty(stack)) {
		stack_element = vbufq_pop(stack);
		vbufq_destroy_stack_element(stack_element);
	}
}

/**
 * @brief deallocate all elements owned by the generator gen
 *
 * @param [in, out] gen - generator to be flushed
 */
static void vbufq_flush_gen(vbufq_msg_box_generator_t *gen)
{
	vbufq_flush_stack(&gen->ready2use);
}

/**
 * @brief initialize intern data of a generator
 *
 * @param [out] gen - generator to be initialized
 */
static void vbufq_init_gen(vbufq_msg_box_generator_t *gen)
{
	gen->ready2use.first = NULL;
	gen->nb_works = 0;
}

#ifdef VBUFQ_STAT

/**
 * @brief get number of of context belonging to a vbufq_link
 *
 * @param [in] vbufq_link
 *
 * @return number of contexts belonging to the vbufq_link
 */
static int vbufq_get_ncontext(vbufq_link_t *vbufq_link)
{
	struct radix_tree_iter iter;
	void **slot;
	int ctx_nb = 0;

	if (vbufq_link == NULL) {
		ETRACE("%s: vbufq_link is null! \n", __func__);
		goto exit;
	}
	mutex_lock(&vbufq_link->mutex);

	radix_tree_for_each_slot (slot, &vbufq_link->ctx_tree, &iter, 0) {
		ctx_nb++;
	}
	mutex_unlock(&vbufq_link->mutex);

exit:
	return ctx_nb;
}
#endif

/**
 * @brief prepare message before sending it to the FE
 *
 * set cookie and request status.
 * @param [in] ctx  - context related to the process sending the request
 * @param [out] msg - message to be prepared
 * @param [in] ret  - status of the fops
 */
static void vbufq_be_make_response(vbufq_be_context_t *ctx, vbufq_msg_t *msg,
				   int ret)
{
	/* fill in header */
	msg->header.be_cookie = ctx;
	msg->status.ret = ret;
	msg->status.success = (ret) ? false : true;
}

/**
 * @brief create a context
 *
 * @param [in] vbufq_link - context to which the context will belong
 * @param [in] name - name of the context
 *
 * @return a context
 */
static vbufq_be_context_t *vbufq_be_ctx_create(vbufq_link_t *vbufq_link,
					       char *name)
{
	vbufq_be_context_t *ctx;
	int diag;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ETRACE("no memory for open context\n");
		goto alloc_failed;
	}

	ctx->vbufq_link = vbufq_link;
	ctx->name = kstrdup(name, GFP_KERNEL);
	ctx->signature = ctx;

	mutex_lock(&vbufq_link->mutex);
	diag = radix_tree_insert(&vbufq_link->ctx_tree, (unsigned long)ctx,
				 ctx);
	mutex_unlock(&vbufq_link->mutex);
	if (diag < 0) {
		ETRACE("error, failed to insert ctx %p\n", ctx);
		goto insert_failed;
	}

	return ctx;

insert_failed:
	vbufq_kfree_and_clear(ctx->name);
	vbufq_kfree_and_clear(ctx);
alloc_failed:
	return ctx;
}

/**
 * @brief destroy a context
 *
 * counterpart of the routine @ref vbufq_be_ctx_create
 * @param [in,out] ctx - context to be destroyed
 * @param locked - is the ctx access already protected by the spinlock of the
 * vbufq_link? If not, the routine will use it then to clean it.
 */
void vbufq_be_ctx_destroy_internal(vbufq_be_context_t *ctx, bool locked)
{
	vbufq_link_t *vbufq_link;
	void *err;

	if (WARN_ON(!ctx)) {
		return;
	}
	vbufq_link = ctx->vbufq_link;

	if (!locked) {
		mutex_lock(&vbufq_link->mutex);
	}

	err = radix_tree_delete(&vbufq_link->ctx_tree, (unsigned long)ctx);
	/* cannot do so much about it ... */
	VBUFQ_ASSERT(err != NULL);
	if (err == NULL) {
		ETRACE("error removing contex %p from  ctx_tree\n", ctx);
	}

	if (!locked) {
		mutex_unlock(&vbufq_link->mutex);
	}

	DTRACE("ctx_release: otherend id: %d, name: %s\n",
	       vmq_peer_osid(vbufq_link->link), ctx->name);

	vbufq_kfree_and_clear(ctx->name);
	vbufq_kfree_and_clear(ctx);
}

/**
 * @brief issue a close operation on the bufq driver
 *
 * @param [in,out] ctx - context fo which a close operation is requested.
 */
static void vbufq_be_native_close(vbufq_be_context_t *ctx)
{
	VBUFQ_ASSERT(ctx->filp != NULL);
	filp_close(ctx->filp, NULL);
	ctx->filp = NULL;
}

/**
 * @brief destroy all contexts related to a vbufq_link
 *
 * @param [out] vbufq_link
 */
static void vbufq_context_list_cleanup(vbufq_link_t *vbufq_link)
{
	vbufq_be_context_t *ctx;
	struct radix_tree_iter iter;
	void **slot;

	if (vbufq_link == NULL) {
		ETRACE("%s: vbufq_link is null! \n", __func__);
		return;
	}

	mutex_lock(&vbufq_link->mutex);
	if (!radix_tree_empty(&vbufq_link->ctx_tree)) {
		radix_tree_for_each_slot (slot, &vbufq_link->ctx_tree, &iter,
					  0) {
			ctx = *slot;
			if (ctx->filp)
				vbufq_be_native_close(ctx);

			vbufq_be_ctx_destroy_locked(ctx);
		}
	}
	mutex_unlock(&vbufq_link->mutex);
}

/**
 * @brief issue an open operation on  bufq driver
 *
 * A filp_open is performed on vbufq_driver, and the associated file ptr is
 * stored in the context
 *
 * @param [in,out] ctx - context for which an open is requested.
 *
 * @return 0 if ok, -ENODEV if an error occurred.
 */
static int vbufq_be_native_open(vbufq_be_context_t *ctx)
{
	mm_segment_t oldfs;
	struct file *filp;
	int ret = 0;

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	filp = filp_open(VBUFQ_BE_BUFQ_DEVICE_NAME, O_RDWR, 0);
	set_fs(oldfs);

	if (IS_ERR(filp)) {
		ret = PTR_ERR(filp);
		goto fail_filpopen;
	}

	ctx->filp = filp;

fail_filpopen:
	return ret;
}

/**
 * @brief process an open request coming from FE
 *
 *  - create a context
 *  - issue an open operation on bufq driver
 *  - prepare response message for the FE.
 *
 * @param [in] vbufq_link - vlink related to the process requesting an open
 * @param [in,out] msg - message embedding the open request. Will be reused
 * for response.
 */
static void vbufq_be_process_open(vbufq_link_t *vbufq_link, vbufq_msg_t *msg)

{
	vbufq_be_context_t *ctx;
	int ret;
#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	vbufq_be_t *be = &vbufq_be;
	struct pv_driver_header_v1_t *be_ver = &be->pv_ver;
	struct pv_driver_header_v1_t *fe_ver = &msg->header.pv_ver;
#endif

	DTRACE("%s OPEN\n", msg->header.name);
	ctx = vbufq_be_ctx_create(vbufq_link, msg->header.name);
	if (IS_ERR_OR_NULL(ctx)) {
		ETRACE("%s: fail to create context\n", msg->header.name);
		ret = -ENOMEM;
		goto fail_ctx_create;
	}
	DTRACE("context properly created\n");

	ret = vbufq_be_native_open(ctx);
	if (ret) {
		ETRACE("%s: fail to open native driver (%d)\n",
			msg->header.name, ret);
		goto fail_native_open;
	}

	/* update response */
	vbufq_be_make_response(ctx, msg, ret);
	STRACE("%s: ctx nb = %d\n", __func__, vbufq_get_ncontext(vbufq_link));
#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	be_ver->fe_version = get_fe_driver_version(fe_ver);
	be_ver->fe_version |= PV_VERSION_VALID_CODE;

	fe_ver->be_version = be_ver->be_version;

	TRACE("fe_version(%#x), be_version(%#x)", be_ver->fe_version,
			be_ver->be_version);
#endif
	return;

fail_native_open:
	vbufq_be_ctx_destroy(ctx);
	ctx = NULL;	/* Returned as msg->header.be_cookie */

fail_ctx_create:
	vbufq_be_make_response(ctx, msg, ret);
}

/**
 * @brief process a close request coming from a FE
 *
 * - issue a close operation on the bufq driver
 * - destroy the context related to the process performing the close operation
 * - update status in the response message
 *
 * @param [in] vbufq_link - vbufq_link from which toe request is coming from
 * @param [in,out] msg - message embedding the close request. Will be reused
 * for response.
 */
static void vbufq_be_process_close(vbufq_link_t *vbufq_link, vbufq_msg_t *msg)
{
	vbufq_be_context_t *ctx;
	vbufq_be_context_t *ctx_check;
	int ret = 0;

	ctx = (vbufq_be_context_t *)msg->header.be_cookie;

	/* check if ctx is known (could be an attack otherwise) */
	mutex_lock(&vbufq_link->mutex);
	ctx_check =
		radix_tree_lookup(&vbufq_link->ctx_tree, (unsigned long)ctx);
	mutex_unlock(&vbufq_link->mutex);
	if (ctx_check == NULL) {
		ETRACE("Unknown context %p\n", ctx);
		ret = -EINVAL;
		goto error;
	}

	if (ctx->signature != ctx) {
		ETRACE("Invalid context (%p:%p)\n", ctx, ctx->signature);
		ret = -EINVAL;
		goto error;
	}

	vbufq_be_native_close(ctx);
	vbufq_be_ctx_destroy(ctx);

error:
	/* update response */
	vbufq_be_make_response(NULL, msg, ret);
	STRACE("%s ctx nb = %d\n", __func__, vbufq_get_ncontext(vbufq_link));
}

/**
 * @brief process an ioctl request coming from a FE.
 * - check input parameters and message signature consistency
 * - extract ioctl parameters from the message
 * - issue the ioctl operation on bufq driver
 * - update operation status in the message
 *
 * @param [in] vbufq_link - vlink related to the process requesting an open
 * @param [in,out] msg - message embedding the ioctl request. Will be reused for response.
 */
static void vbufq_be_process_ioctl(vbufq_link_t *vbufq_link, vbufq_msg_t *msg)
{
	mm_segment_t oldfs;
	vbufq_be_context_t *ctx;
	vbufq_be_context_t *ctx_check;
	struct vbufq_msg_header *header;
	struct vbufq_msg_status *status;
	const struct file_operations *fops;
	unsigned int cmd;
	const char *cmd_name;
	int ret = 0;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	int fence_fd = -1;
#endif

	header = &msg->header;
	status = &msg->status;

	ctx = (vbufq_be_context_t *)header->be_cookie;
	cmd = header->cmd;

	/* check if ctx is known (could be an attack otherwise) */
	mutex_lock(&vbufq_link->mutex);
	ctx_check =
		radix_tree_lookup(&vbufq_link->ctx_tree, (unsigned long)ctx);
	mutex_unlock(&vbufq_link->mutex);
	if (ctx_check == NULL) {
		ETRACE("Unknown context %p\n", ctx);
		ret = -EINVAL;
		goto error;
	}

	if (ctx->signature != ctx) {
		ETRACE("fe_cookie: 0x%p, ctx is invalid(0x%p:0x%p)\n",
		       header->fe_cookie, ctx, ctx->signature);
		ret = -EINVAL;
		goto error;
	}

	cmd_name = vbufq_cmd2name(cmd);

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	/*
	 * Samsung plans to add fence support in BQ_BUF_ENQUEUE and
	 * BQ_BUF_ACQUIRE. for now there will be no fence support in
	 * BQ_BUF_DEQUEUE and BQ_BUF_RELEASE. Note that BQ_BUF_ENQUEUE and
	 * BQ_BUF_DEQUEUE ioctls (UBM producer) are handled by vBUFQ.
	 * BQ_BUF_ACQUIRE and BQ_BUF_RELEASE ioctls (UBM consumer) are handled
	 * directly by the BUFQ driver. As a consequence and for now vBUFQ
	 * should add fence support only in BQ_BUF_ENQUEUE.
	 */
	if (cmd == BQ_BUF_ENQUEUE) {
		if (msg->payload.bufq_buf.fence_fd == VFENCE2_ID_NONE) {
			msg->payload.bufq_buf.fence_fd = -1;
		} else {
			ret = vfence2_import_fence(vbufq_link->vfence2_connection,
						   msg->payload.bufq_buf.fence_fd,
						   &fence_fd);
			if (ret) {
				ETRACE("ctx %s cmd %08x (%s) vfence2_import_fence "
				       "failed (%d)\n", ctx->name, cmd, cmd_name, ret);
				goto error;
			}
			msg->payload.bufq_buf.fence_fd = fence_fd;
		}
	}
#endif

	oldfs = get_fs();
	set_fs(KERNEL_DS);
	fops = ctx->filp->f_op;

	ret = fops->unlocked_ioctl(ctx->filp, cmd,
				   (unsigned long)&msg->payload);
	if (ret) {
		const char *name = vbufq_cmd2name(header->cmd);
		ETRACE("%s: fail: fe_cookie(0x%p): cmd: %s, ret: %d\n",
		       __func__, header->fe_cookie, name, ret);
	}

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (fence_fd >= 0) {
#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
		const int ret2 = ksys_close(fence_fd);
#else
		const int ret2 = sys_close(fence_fd);
#endif

		if (ret2) {
			WTRACE("ctx %s fence ksys_close(%d) failed (%d)\n",
				ctx->name, fence_fd, ret2);
		}
		fence_fd = -1;
	}
#endif

	set_fs(oldfs);

error:
	/* update response */
	vbufq_be_make_response(ctx, msg, ret);
}

/**
 * @brief send the appropriated request to bufq driver depending on message content
 *
 * @param vbufq_link - vbufq_link from which the request is coming from
 * @param msg - message embedding the request.
 *
 * @return 0 if OK, -EINVAL otherwise
 */
static int vbufq_be_req_process(vbufq_link_t *vbufq_link, vbufq_msg_t *msg)
{
	int ret = 0;

	DTRACE("entering vbufq_be_req_process\n");
	switch (msg->header.cmd_type) {
	case VBUFQ_CMD_FILE_OPEN:
		DTRACE("before FW OPEN\n");
		vbufq_be_process_open(vbufq_link, msg);
		break;
	case VBUFQ_CMD_FILE_CLOSE:
		DTRACE("before FW CLOSE\n");
		vbufq_be_process_close(vbufq_link, msg);
		break;
	case VBUFQ_CMD_IOCTL:
		DTRACE("before FW IOCTL\n");
		vbufq_be_process_ioctl(vbufq_link, msg);
		break;

	default:
		ETRACE("fe_cookie(0x%p): invalid cmd type: %d\n",
		       msg->header.fe_cookie, msg->header.cmd_type);
		ret = -EINVAL;
	}

	return ret;
}

/**
 * @brief work cb feeding the worqueue with incomming messages
 *
 * @param work
 */
static void vbufq_be_feeder_cb(struct work_struct *work)
{
	vbufq_msg_t *msg = NULL;
	vbufq_msg_box_t *msg_box = NULL;
	vbufq_link_t *vbufq_link =
		container_of(work, vbufq_link_t, feeder_work);

	DTRACE("BEFORE VMQ_MSG_RECEIVE\n");
	while (!vmq_msg_receive(vbufq_link->link, (void *)&msg)) {
		msg_box = vbufq_get_msg_box(&vbufq_link->gen);
		if (msg_box == NULL) {
			ETRACE("error, was not able to get a msgbox, dropping the message\n");
			//unlock the frontend, since the request cannot be issued
			msg->status.ret = -ENOMEM;
			msg->status.success = false;
			vmq_msg_return(vbufq_link->link, msg);
			continue;
		}
		msg_box->vbufq_link = vbufq_link;
		msg_box->msg = msg;
		queue_work(vbufq_link->wq, &msg_box->work);
	}
}

static void vbufq_work_cb(struct work_struct *work)
{
	vbufq_msg_box_t *msg_box =
		container_of(work, struct vbufq_msg_box_t, work);
	vbufq_link_t *vbufq_link = msg_box->vbufq_link;
	DTRACE("in a worker\n");

	vbufq_be_req_process(vbufq_link, msg_box->msg);

	vmq_msg_return(vbufq_link->link, msg_box->msg);

	vbufq_release_msg_box(&vbufq_link->gen, msg_box);
}

/*----- VMQ event handlers (callbacks) -----*/

/*
 *  Callback from vlx-vmq.c in the context of vbufq_thread().
 */

/**
 * @brief Callback triggered when a connection to a FE is ongoing.
 *
 * It is one of the VMQ callbacks, from vlx-vmq.c in the context of
 * vbufq_thread().
 *
 * @param [in] link - the link on which a FE is available for connection.
 */
static void vbufq_cb_link_on(vmq_link_t *link)
{
	vbufq_link_t *vbufq_link = VBUFQ_LINK(link);

	DTRACE("link on (local <-> OS %d).\n", vmq_peer_osid(link));
	vbufq_link->is_up = true;
}

/**
 * @brief callback triggered when a disconnection to a FE happened
 *
 * It is one of the VMQ callbacks, from vlx-vmq.c in the context of
 * vbufq_thread().
 *
 * memory dynamically allocated for all contexts related to the link should be
 * deallocated here.
 *
 * @param link
 */

static void vbufq_cb_link_off(vmq_link_t *link)
{
	vbufq_link_t *vbufq_link;
	VBUFQ_ASSERT(link != NULL);

	vbufq_link = VBUFQ_LINK(link);

	DTRACE("link off %d\n", vmq_peer_osid(link));
	if (false == vbufq_link->is_up) {
		DTRACE("vbufq_cb_link_off called before up\n");
		return;
	}

	if (vbufq_link->wq) {
		drain_workqueue(vbufq_link->wq);
	}
	vbufq_flush_gen(&vbufq_link->gen);
	vbufq_init_gen(&vbufq_link->gen);

	/* remove all resources */
	vbufq_context_list_cleanup(vbufq_link);

	STRACE("%s: ctx nb = %d\n", __func__, vbufq_get_ncontext(vbufq_link));

	vbufq_link->is_up = false;
}

/**
 * @brief VMQ callback.
 *
 * notify the vbufq_thread that a sysconf event occurred.
 * @param [in] - links list of links related to the sysconf event
 */

static void vbufq_cb_sysconf_notify(vmq_links_t *links)
{
	vbufq_be_t *be = VBUFQ_LINKS(links);

	be->is_sysconf = true;
	up(&be->thread_sem);
}

/**
 * @brief vmq callback, triggered when a message is received
 * Trigger workers managing messages.
 * @param link
 */
static void vbufq_cb_receive_notify(vmq_link_t *link)
{
	vbufq_link_t *vbufq_link = VBUFQ_LINK(link);
	DTRACE("receiving a message, poking working thread\n");
	queue_work(vbufq_link->wq, &vbufq_link->feeder_work);
}

static const vmq_callbacks_t vmq_callbacks = {
	.link_on = vbufq_cb_link_on,
	.link_off = vbufq_cb_link_off,
	.sysconf_notify = vbufq_cb_sysconf_notify,
	.receive_notify = vbufq_cb_receive_notify,
	.get_rx_config = vbufq_cb_get_xx_config,
	.get_tx_config = vbufq_cb_get_xx_config,
};

/**
 * @brief initialize a link
 * called in @ref vbufq_vmq_init.
 * Initialize all data of a vbufq_link.
 * the counterpart of this routine is @ref vbufq_link_free
 * @param link - link to be initialized
 * @param cookie
 *
 * @return whether to stop iterating
 */
static bool vbufq_link_init(vmq_link_t *link, void *cookie)
{
	vbufq_link_t *vbufq_link = VBUFQ_LINK(link);
	char wq_name[VBUFQ_BE_NAME_MAX_SIZE];

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	vbufq_be_t *be = cookie;
	int ret = vfence2_connect(be->vfence2_client, vmq_peer_osid(link),
				  &vbufq_link->vfence2_connection);
	if (ret) {
		ETRACE("vfence2_connect(%d) failed (%d)\n", vmq_peer_osid(link),
			ret);
		return true;	/* Stop iterating */
	}
#endif

	vbufq_link->link = link;
	vbufq_link->is_up = false;
	vbufq_link->is_thread_aborted = false;
	INIT_WORK(&vbufq_link->feeder_work, vbufq_be_feeder_cb);

	mutex_init(&vbufq_link->mutex);
	INIT_RADIX_TREE(&vbufq_link->ctx_tree, GFP_KERNEL);
	vbufq_init_gen(&vbufq_link->gen);

	//init of the workqueue
	scnprintf(wq_name, sizeof(wq_name), "%s.wq.%d", VBUFQ_BE_NAME,
		  vmq_peer_osid(link));

	vbufq_link->wq = alloc_workqueue(
		wq_name, (WQ_UNBOUND | WQ_HIGHPRI | WQ_MEM_RECLAIM), 0);

	if (!vbufq_link->wq) {
		ETRACE("Failed to create vbufq_be_wq workqueue \n");
		return true;
	}
	return false;
}

static bool vbufq_link_count_vlinks(vmq_link_t *link, void *cookie)
{
	int *nb_devices = cookie;
	(*nb_devices)++;
	return false;
}
/* VMQ thread */

/**
 * @brief  vmq thread, used for VMQ callback management.
 *
 * @param arg
 *
 * @return
 */
static int vbufq_thread(void *arg)
{
	vbufq_be_t *be = arg;

	DTRACE("started\n");
	while (!be->is_thread_aborted) {
		int diag = down_interruptible(&be->thread_sem);

		(void)diag;
		DTRACE("wakeup%s%s\n",
		       be->is_thread_aborted ? " thread-aborted" : "",
		       be->is_sysconf ? " sysconf" : "");
		if (be->is_thread_aborted)
			break;
		if (be->is_sysconf) {
			be->is_sysconf = false;
			DTRACE("before calling vmq_links_sysconf\n");
			vmq_links_sysconf(be->links);
			DTRACE("after calling vmq_links_sysconf\n");
		}
	}
	DTRACE("exiting\n");
	return 0;
}

/**
 * @brief initialize all the VMQ boilerplate.
 *
 * @return
 */
static int vbufq_vmq_init(void)
{
	int ret;
	int nb_vlinks = 0;
	vbufq_be_t *be = &vbufq_be;

	/*  Semaphore init */
	sema_init(&be->thread_sem, 0); /* Before it is signalled */

	ret = vmq_links_init_ex(&be->links, "bufq", &vmq_callbacks,
				&vbufq_rx_config_be2fe, NULL, be, false);

	if (ret) {
		ETRACE("%s cannot initialize VMQ vlinks err=%d\n", __func__,
		       ret);
		return ret;
	}

	//count number of registered vlinks. If 0, exit
	vmq_links_iterate(be->links, vbufq_link_count_vlinks, &nb_vlinks);
	if (nb_vlinks == 0) {
		ret = -EIO;
		ETRACE("error, no vlink with valid configuration found, leaving\n");
		/* Must free be->links object */
		goto no_vlinks;
	}
	TRACE("nb_vlink = %d\n", nb_vlinks);

	ret = vmq_links_iterate(be->links, vbufq_link_init, be);
	if (ret) {
		ETRACE("%s VMQ link init failed\n", __func__);
		ret = -EIO;
		goto vlink_init_failed;
	}

	ret = vmq_links_start(be->links);
	if (ret) {
		ETRACE("%s VMQ link start failed\n", __func__);
		ret = -EIO;
		goto vmq_links_start_failed;
	}

	/* Initialize kthreads */
	ret = vlx_thread_start(&be->vmq_thread_desc, vbufq_thread, be,
			       "vbufq-be");
	if (ret) {
		ETRACE("thread start failure\n");
		ret = -EIO;
		goto vlx_thread_start_failed;
	}

	return ret;
vlx_thread_start_failed:
vmq_links_start_failed:
	vmq_links_stop(be->links);
vlink_init_failed:
	vmq_links_iterate(be->links, vbufq_link_free, be);
no_vlinks:
	vmq_links_finish(be->links);
	return ret;
}

/**
 * @brief request vmq_thread to abort
 *
 *
 * @param be
 */
static void vbufq_thread_aborted_notify(vbufq_be_t *be)
{
	be->is_thread_aborted = true;
	up(&be->thread_sem);
}

/* Only called from vbufq_exit() */

/**
 * @brief  clean all data related to a link
 *
 * @param link - link to be cleaned
 * @param cookie
 *
 * @return
 */
static _Bool vbufq_link_free(vmq_link_t *link, void *cookie)
{
	vbufq_link_t *vbufq_link = VBUFQ_LINK(link);

	DTRACE("link_free on (local <-> OS %d).\n", vmq_peer_osid(link));
	/* Check if ever initialized */
	if (!vbufq_link)
		return false;
	if (vbufq_link->wq) {
		destroy_workqueue(vbufq_link->wq);
		vbufq_link->wq = NULL;
	}

	//cleaning all contexts related to the current link
	vbufq_context_list_cleanup(vbufq_link);

	vbufq_kfree_and_clear(vbufq_link->device_name);

	STRACE("%s: ctx nb = %d\n", __func__, vbufq_get_ncontext(vbufq_link));

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	/* If vfence2_connect() fails, we come here with NULL pointers */
	if (vbufq_link->vfence2_connection) {
		(void) vfence2_disconnect(vbufq_link->vfence2_connection);
	}
#endif

	vbufq_kfree_and_clear(vbufq_link);
	VBUFQ_LINK(link) = NULL;
	return false;
}

/*
 * @brief usual init_module routine
 *
 * @return
 */
static int __init vbufq_be_init(void)
{
	vbufq_be_t *be = &vbufq_be;
	int ret;

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	ret = vfence2_create_client(VBUFQ_BE_NAME, &be->vfence2_client);
	if (ret) {
		ETRACE("vfence2_create_client() failed (%d)\n", ret);
		return ret;
	}
#endif

	ret = vbufq_vmq_init();
	if (ret) {
		ETRACE("failed to initialize vmq\n");
		goto cleanup;
	}

#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	/*
	 * Set backend driver version
	 */
	set_be_driver_version(&be->pv_ver, VBUFQ_PV_VERSION);
#endif

	return ret;

cleanup:
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (be->vfence2_client) {
		int ret2 = vfence2_destroy_client(be->vfence2_client);
		if (ret2) {
			WTRACE("vfence2_destroy_client() failed (%d)\n", ret2);
		}
		be->vfence2_client = NULL;
	}
#endif
	return ret;
}

/*
 * @brief usual module_exit routine
 *
 * @return
 */
static void vbufq_be_exit(void)
{
	vbufq_be_t *be = &vbufq_be;

	DTRACE("exiting\n");
	if (be->links) {
		vmq_links_abort(be->links);
	}
	vbufq_thread_aborted_notify(be);
	vlx_thread_join(&be->vmq_thread_desc);
	DTRACE("after join\n");
	if (be->links) {
		vmq_links_iterate(be->links, vbufq_link_free, NULL);
		vmq_links_finish(be->links);
		be->links = NULL;
	}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (be->vfence2_client) {
		const int ret = vfence2_destroy_client(be->vfence2_client);

		if (ret) {
			WTRACE("vfence2_destroy_client() failed (%d)\n", ret);
		}
		be->vfence2_client = NULL;
	}
#endif
}
module_init(vbufq_be_init);
module_exit(vbufq_be_exit);

/*----- Module description -----*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Lavocat <nicolas.lavocat@harman.com>");
MODULE_DESCRIPTION("Vbufq FE driver");
/*----- End of file -----*/
