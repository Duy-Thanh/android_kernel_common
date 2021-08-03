/* SPDX-License-Identifier: GPL-2.0+ */
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

#ifndef __VBUFQ_BE_H__
#define __VBUFQ_BE_H__

/*----- System header files -----*/

#include <linux/module.h> /* __exit, __init */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 7)
/* This include exists in 2.6.6 but functions are not yet exported */
#include <linux/kthread.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#include <linux/semaphore.h>
#endif
#include <linux/init.h> /* module_init() in 2.6.0 and before */
#include <linux/slab.h>
#include "vbufq_common.h"
#include "vlx-vipc.h"
#include <vlx/vdriver-lib.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <uapi/linux/bufq.h>
#include <linux/radix-tree.h>
#include <linux/mutex.h>

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
#include <linux/syscalls.h>
#include "vfence2_kapi.h"
#endif

/*----- Local configuration -----*/

#if 0
#define VBUFQ_DEBUG
#endif

#if 0
#define VBUFQ_STAT
#endif

#if 0
#define VBUFQ_ASSERTS
#endif
/*----- Tracing -----*/

#ifdef VBUFQ_DEBUG
#define DTRACE(x...)                                                           \
	do {                                                                   \
		printk("(%d) BE %s: ", current->tgid, __func__);               \
		printk(x);                                                     \
	} while (0)
#else
#define DTRACE(x...)
#endif

#ifdef VBUFQ_STAT
#define STRACE(x...) printk(KERN_CRIT "VBUFQ_BE: STAT" x)
#else
#define STRACE(x...)
#endif

#ifdef VBUFQ_ASSERTS
#define VBUFQ_ASSERT(c)                                                        \
	do {                                                                   \
		if (!(c))                                                      \
			BUG();                                                 \
	} while (0)
#else
#define VBUFQ_ASSERT(c)
#endif

#define TRACE(x...) printk(KERN_INFO "VBUFQ_BE: " x)
#define WTRACE(x...) printk(KERN_WARNING "VBUFQ_BE: " x)
#define ETRACE(x...) printk(KERN_ERR "VBUFQ_BE: " x)

#define VLX_SERVICES_THREADS
#include "vlx-services.c"

#define VBUFQ_LINK(link)                                                       \
	(*(vbufq_link_t **)&((vmq_link_public_t *)(link))->priv)

#define VBUFQ_LINKS(links) ((vbufq_be_t *)((vmq_links_public *)(links))->priv)
#undef VBUFQ_LINKS
#define VBUFQ_LINKS(links) (*(vbufq_be_t **)(links))

#define VBUFQ_BE_NAME "vbufq-be"
#define VBUFQ_BE_NAME_MAX_SIZE 16
typedef struct vbufq_be_link_t vbufq_link_t;

/**
 * @brief container created in order to embed a message, used by thread of work queues.
 *
 * Thus, msg is reachable from work callbacks, using the macro container_of
 */
typedef struct vbufq_msg_box_t {
	vbufq_msg_t *msg; /**<message coming from the fe */
	struct work_struct
		work; /**<work object that will be provided to the workqueue */
	vbufq_link_t *
		vbufq_link; /**<permits to notify to workqueue callbacks from which VM the message is coming from  */
} vbufq_msg_box_t;

/**
 * @brief an element of a @ref vbufq_stack_t.
 */
typedef struct vbufq_stack_element_t {
	struct vbufq_stack_element_t *next; /**<next element of the stack */
	vbufq_msg_box_t msg_box; /**<were useful data are embedded */
} vbufq_stack_element_t;

/**
 * @brief structure used for a stack implementation
 */
typedef struct vbufq_stack_t {
	vbufq_stack_element_t
		*first; /**<point to the first element of the stack  */
} vbufq_stack_t;

/**
 * @brief  describe a message box generator
 */
typedef struct vbufq_msg_box_generator_t {
	vbufq_stack_t
		ready2use; /**<stack containing msg_box ready to use, released by producers */
	unsigned int nb_works; /**<number of mesg box created */
} vbufq_msg_box_generator_t;

/**
 * @brief Global data in the driver. A global instance is present the driver.
 */
typedef struct vbufq_be_t {
	struct semaphore
		thread_sem; /**<semaphore used to wakeup the VMQ housekeeping thread */
	_Bool is_thread_aborted; /**<notify is the VMQ housekeeping thread has been waked up in order to abort. */
	_Bool is_sysconf; /**<notify is the VMQ housekeeping thread has been waked up for a sysconf event */
	vlx_thread_t
		vmq_thread_desc; /**<thread descriptor for the VMQ housekeeping thread */
	vmq_links_t *links; /**<list of all VMQ links used */
#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	struct pv_driver_header_v1_t pv_ver;
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_client *vfence2_client;
#endif
} vbufq_be_t;

/**
 * @brief Represent a connection between a frontend and a backend.
 */
struct vbufq_be_link_t {
	vmq_link_t *link; /**<VMQ link representing the connection */
	_Bool is_thread_aborted; /**<notify if the feeding thread has been waked up in order to abort. */
	struct mutex
		mutex; /**<mutex permitting to control access to the @ref ctx_tree. */
	struct radix_tree_root
		ctx_tree; /**<contains all contexts related to this connection  */
	_Bool is_up; /**<notify if the connection is on, i.e fe,be have detected each other. */
	struct workqueue_struct *
		wq; /**<workqueue handling the forwarding of requests/responses to/from between bufq and fe. */
	vbufq_msg_box_generator_t
		gen; /**<message box generator used for the work pool management. */
	char *device_name; /**<name of the char device created to reach the fe related to this connection. */
	vmq_xx_config_t
		xx_config; /**<contains VMQ configuration, dynamically get from the device tree, for rx configuration. */
	struct work_struct
		feeder_work; /**<work object dedicated for the feeder of this vbufq_link*/
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_connection *vfence2_connection;
#endif
};

/**
 * @brief Represent a user process.
 */
typedef struct vbufq_be_context_t {
	char *name; /**<name of the related process */
	vbufq_link_t *
		vbufq_link; /**<vbufq_link to which the context is related to */
	struct file *filp; /**<file descriptor for bufq device interface */
	void *signature; /**<permits to check the consistency of a ctx */

} vbufq_be_context_t;

/*  Functions */
#define vbufq_be_ctx_destroy_locked(ctx)                                       \
	vbufq_be_ctx_destroy_internal(ctx, true)
#define vbufq_be_ctx_destroy(ctx) vbufq_be_ctx_destroy_internal(ctx, false)

#endif /* __VBUFQ_BE_H__ */
