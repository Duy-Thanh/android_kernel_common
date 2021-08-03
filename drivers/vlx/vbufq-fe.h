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
#ifndef _VBUF_Q_FE_H_
#define _VBUF_Q_FE_H_

#define VBUFQ_DEVICE_NAME "vbufq_fe" // should be same the bufq device name

/*----- System header files -----*/

#include <linux/module.h> /* __exit, __init */
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/miscdevice.h>
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
#include <linux/uaccess.h>
#include <uapi/linux/bufq.h>
#include <linux/wait.h>
#include <linux/poll.h>

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
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
		printk("(%d) FE %s: ", current->tgid, __func__);               \
		printk(x);                                                     \
	} while (0)
#else
#define DTRACE(x...)
#endif

#ifdef VBUFQ_STAT
#define STRACE(x...) printk(KERN_CRIT "VBUFQ_BE: STAT " x)
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

#define TRACE(x...) printk(KERN_INFO "VBUFQ_FE: " x)
#define WTRACE(x...) printk(KERN_WARNING "VBUFQ_FE: " x)
#define ETRACE(x...) printk(KERN_ERR "VBUFQ_FE: " x)

#define VLX_SERVICES_THREADS
#include "vlx-services.c"

#define VBUFQ_LINK(link)                                                       \
	(*(vbufq_link_t **)&((vmq_link_public_t *)(link))->priv)

#define VBUFQ_LINKS(links) ((vbufq_fe_t *)((vmq_links_public *)(links))->priv)
#undef VBUFQ_LINKS
#define VBUFQ_LINKS(links) (*(vbufq_fe_t **)(links))

typedef struct vbufq_fe_link_t vbufq_link_t;

typedef struct vbufq_device_t {
	struct class *class;
	int dev_major;
	unsigned int ndevices;
	vbufq_link_t **minor2vbufq_link;
	unsigned int nregistered_dev;
} vbufq_device_t;

/**
 * @brief Global data in the driver. A global instance is present the driver.
 */
typedef struct vbufq_fe_t {
	struct semaphore
		thread_sem; /**<semaphore used to wakeup the VMQ housekeeping thread */
	_Bool is_thread_aborted; /**<notify is the VMQ housekeeping thread has been waked up in order to abort. */
	_Bool is_sysconf; /**<notify is the VMQ housekeeping thread has been waked up for a sysconf event */
	vlx_thread_t
		thread_desc; /**<thread descriptor for the VMQ housekeeping thread */
	vmq_links_t *links; /**<list of all VMQ links used */
	vbufq_device_t
		info_devices; /**<contains info related to the class device created for the fe. */
#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	struct pv_driver_header_v1_t pv_ver;
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_client *vfence2_client;
#endif
} vbufq_fe_t;

/**
 * @brief Represent a connection between a frontend and a backend.
 */
struct vbufq_fe_link_t {
	vmq_link_t *link; /**<VMQ link representing the connection */
	vipc_ctx_t
		vipc_ctx; /**<used by the vipc library, permitting to issue synchronous requests with VMQ*/
	_Bool is_up; /**<notify if the connection is on, i.e fe,be have detected each other. */
	spinlock_t
		slock; /**<spinlock permitting to control access to the @ref ctx_list */
	struct list_head
		ctx_list; /**<list of all contexts related to this connection  */
	unsigned int
		dev_minor; /**<minor of the char device created to reach the fe related to this connection */
	char *device_name; /**<name of the char device created to reach the fe related to this connection */
	vmq_xx_config_t
		xx_config; /**<contains VMQ configuration, dynamically get from the device tree, for tx configuration */
	_Bool requests_are_allowed; /**<notify if new requests coming from user process are allowed or not. */
	wait_queue_head_t
		wait_queue; /**<used for poll, waiting for be connection   */
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_connection *vfence2_connection;
#endif
};

/**
 * @brief Represent a user process.
 */
typedef struct vbufq_fe_context_t {
	vbufq_link_t *
		vbufq_link; /**<vbufq_link to which the context is related to */
	char *name; /**<name of the related process */
	struct list_head
		node; /**<permits to keep track of the context in the ctx_list of a @ref vbufq_fe_link_t "vbufq_link_t" structure */
	void *cookie; /**<RW by be, permits to get the be ctx associated to a de ctx */
	void *signature; /**<permits to check the consistency of a ctx */
	unsigned int
		req_cmpt; /**<number of requests issued by the process, for debugging log purpose */
	_Bool is_valid; /**< false if a backend disconnection occured on its vbufq_link, true otherwise */
} vbufq_fe_context_t;

#define vbufq_context_destroy_locked(ctx)                                      \
	vbufq_context_destroy_internal(ctx, true)
#define vbufq_context_destroy(ctx) vbufq_context_destroy_internal(ctx, false)

#endif /* _VBUF_Q_FE_H_  */
