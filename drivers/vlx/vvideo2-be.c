// SPDX-License-Identifier: GPL-2.0+
/*
 ****************************************************************
 *
 *  Component: VLX virtual video v2 backend driver
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
 *    Christophe Lizzi (Christophe.Lizzi@harman.com)
 *    Adam Mirowski (adam.mirowski@harman.com)
 *
 ****************************************************************
 */

/*----- System includes -----*/

#include <linux/device.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#if KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE
#include <linux/sched.h>
#else
#include <linux/sched/types.h>
#include <linux/sched/task.h>
#endif
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif
#include <linux/dma-buf.h>
#include <linux/videodev2.h>
#include <linux/syscalls.h>
#include <linux/delay.h>
#include <linux/fdtable.h>

#include <nk/nkern.h>

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
#include "vfence2_kapi.h"
#endif

/*----- Local includes -----*/

#include <vlx/vvideo2_common.h>
#include "vvideo-util.h"
#include "vlx-vmq.h"

#ifndef VVIDEO_LOCAL_GID
#include "vdmaheap_kapi.h"
#endif

#define VLX_SERVICES_THREADS
#include "vlx-services.c"

/*----- Configuration -----*/

/* Enable logging messages at all (except VVIDEO_DMABUF) */
// #define VVIDEO_DEBUG

/* Enable low-level communications traces */
// #define VVIDEO_COMMS

/* Enable verbose traces */
// #define VVIDEO_VERBOSE

/* Enabled 1 second sleep after /dev/video* device close */
// #define VVIDEO_MSLEEP_CLOSE

/* Use integer file descriptors rather than struct file* objects */
// #define VVIDEO_USE_FD

/* Enable DMABUF traces */
// #define VVIDEO_DMABUF

/* Enable asserts */
#define VVIDEO_ASSERTS

/* Remember previous transactions per vlink */
// #define VVIDEO_REMEMBER_TRANSACTIONS
// #define VVIDEO_LAST_TRANSACTIONS 4

/* Debug native drivers returning stale b->index */
// #define VVIDEO_DQBUF

/*
 * Consider Decoded Pixel Buffer (DPB) file descriptors in
 * MFC user shared memory during DQBUF processing.
 */
#define VVIDEO_DPBS

/* Max DMABUFs per context */
#define VVIDEO_DMABUF_MAX 80

/* Highest buf_type */
#define VVIDEO_BUF_TYPE_MAX 16

/* Max open contexts per video device */
#define VVIDEO_CTX_MAX 64

/* Max running polls per context (for security only) */
#define VVIDEO_CTX_POLL_MAX 32

/* Max count of printed errors per category */
#define VVIDEO_ERR_MAX 10

/* Max count of printed warnings per category */
#define VVIDEO_WARN_MAX 10

/* Use struct poll_wqueues instead of custom structures */
/* This code is experimental, used to compile but never worked */
// #define VVIDEO_POLL_WQUEUES

#ifndef VVIDEO_POLL_WQUEUES
/* At least 3 required for v4l2 */
#define VVIDEO_POLL_MAX	3
#endif

#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
/*
 * The version of vvideo driver should be able to update without
 * dependency between VMs.
 */
#define VVIDEO_BE_VERSION PV_DRV_VERSION(3, 0, 0)
#endif

/* Make locating issues easier during debugging */
#define VVIDEO_NOINLINE
// #define VVIDEO_NOINLINE	__attribute__((__noinline__))

/*
 * Remove /proc/nk/vvideo2-be as late as possible during module unload.
 * This option is for debug only, as not enough synchronization is
 * done to prevent parallel object destruction while they are observed.
 */
// #define VVIDEO_LATE_PROC_EXIT

/* Explicit fence flags */
#ifndef V4L2_BUF_FLAG_IN_FENCE
#define V4L2_BUF_FLAG_IN_FENCE 0x00200000
#endif
#ifndef V4L2_BUF_FLAG_OUT_FENCE
#define V4L2_BUF_FLAG_OUT_FENCE 0x00400000
#endif
#ifndef V4L2_CAP_FENCES
#define V4L2_CAP_FENCES 0x20000000
#endif

#ifndef VVIDEO_LOCAL_GID
#define VVIDEO_VDMAHEAP_GRACE_DELAY_MS 2000
#endif

/*----- Logging -----*/

#define VVIDEO_DRIVER "vvideo2-be"
#define VVIDEO_MSG "VVIDEO2-BE: "

#ifdef VVIDEO_DEBUG
#define VVIDEO_LOG(format, args...)                                            \
	pr_info(VVIDEO_MSG "%s: " format, __func__, ##args)
#else
#define VVIDEO_LOG(format, args...)                                            \
	do {                                                                   \
	} while (0)
#endif

#ifdef VVIDEO_COMMS
#define VVIDEO_LOG_COMMS(format, args...) VVIDEO_LOG(format, ##args)
#else
#define VVIDEO_LOG_COMMS(format, args...)                                      \
	do {                                                                   \
	} while (0)
#endif

#ifdef VVIDEO_VERBOSE
#define VVIDEO_LOG_VERBOSE(format, args...) VVIDEO_LOG(format, ##args)
#else
#define VVIDEO_LOG_VERBOSE(format, args...)                                    \
	do {                                                                   \
	} while (0)
#endif

#ifdef VVIDEO_DMABUF
#define VVIDEO_LOG_DMABUF(format, args...)                                     \
	pr_info(VVIDEO_MSG "%s: " format, __func__, ##args)
#else
#define VVIDEO_LOG_DMABUF(format, args...)                                     \
	do {                                                                   \
	} while (0)
#endif

#define VVIDEO_ERR(format, args...)                                            \
	(pr_err(VVIDEO_MSG "%s: ERROR: " format, __func__, ##args))
#define VVIDEO_ERR_LIMITED(_counter, format, args...)                          \
	do {                                                                   \
		if (++(_counter) <= VVIDEO_ERR_MAX)                            \
			VVIDEO_ERR(format, ##args);                            \
	} while (0)
#define VVIDEO_WARN(format, args...)                                           \
	(pr_warn(VVIDEO_MSG "%s: WARNING: " format, __func__, ##args))
#define VVIDEO_WARN_LIMITED(_counter, format, args...)                         \
	do {                                                                   \
		if (++(_counter) <= VVIDEO_WARN_MAX)                           \
			VVIDEO_WARN(format, ##args);                           \
	} while (0)
#define VVIDEO_INFO(format, args...) (pr_notice(VVIDEO_MSG format, ##args))

/*----- Definitions -----*/

/*
 * Hold entry until a QBUF, which replaces it with QUEUED,
 * or until VIDIOC_STREAMOFF cancels it.
 */
#define VVIDEO_DMABUF_PREPARED	0x1
/*
 * Holds entry until some VIDIOC_DQBUF returns it, maybe much
 * later, or until VIDIOC_STREAMOFF cancels it.
 */
#define VVIDEO_DMABUF_QUEUED	0x2
/*
 * Holds entry until ioctl returns, to be able to remap
 * gid in original request back to fd, in case it got
 * somehow changed by native driver (very unlikely).
 */
#define VVIDEO_DMABUF_RUNNING	0x4
/*
 * Match deleted entries if native driver mistakenly
 * returns same b->index twice from VIDIOC_DQBUF and
 * in other circumstances.
 */
#define VVIDEO_DMABUF_DELETED	0x8
/* For final cleanup */
#define VVIDEO_DMABUF_ALL	(-1)

struct vvideo_dmabuf_t {
	struct vvideo_ctx_t *ctx;
	struct vvideo_dmabuf_gid_t gid;
	struct dma_buf *dma_buf;
	int fd;
	int buf_index;	/* For information only */
	int plane;	/* For info only, e.g. 0=Luminance, 1=Chrominance */
	enum v4l2_buf_type buf_type;
	struct kref kref; /* vvideo_dev_t::lock */
	int saved_f_count; /* For information only */
	/* Status */
	_Bool valid;	/* Entry is valid */
	_Bool prepared;	/* Whether command has been issued */
	int queued;	/* How many times queued. Usually only 1 works */
	int running;	/* How many ioctls referencing are running */
};

enum vvideo_ctx_state_t {
	VVIDEO_CS_FREE, /* Set by vvideo_ctx_free() */
	VVIDEO_CS_BEING_OPENED, /* Set by vvideo_ctx_alloc() */
	VVIDEO_CS_OPEN, /* Set by vvideo_req_open() */
	VVIDEO_CS_CLOSE_PENDING, /* Set by vvideo_req_release() */
	/* Set by vvideo_ctx_release() and vvideo_dev_release_ctxs() */
	VVIDEO_CS_BEING_CLOSED
};

#if KERNEL_VERSION(4, 12, 0) > LINUX_VERSION_CODE
#define VVIDEO_WAIT_QUEUE_ENTRY wait_queue_t
#else
#define VVIDEO_WAIT_QUEUE_ENTRY wait_queue_entry_t
#endif

#ifndef VVIDEO_POLL_WQUEUES
/*
 * One per poll_wait() callback from native driver's f_op::poll().
 * Similar to "struct poll_table_entry" in include/poll.h
 */
struct vvideo_poll_table_entry_t {
	/* No struct file pointer */
	/* No key */
	VVIDEO_WAIT_QUEUE_ENTRY wq;
	wait_queue_head_t *wqh;
};
#endif

/* One per active select/poll in context */
struct vvideo_poll_t {
	/* Fields set at init time once for all */
	struct vvideo_ctx_t *ctx;
	unsigned long long mask;
#if defined VVIDEO_DEBUG || defined VVIDEO_VERBOSE
	char name[32];
#endif
	/* Work variables */
#ifdef VVIDEO_POLL_WQUEUES
	struct poll_wqueues poll_wqueues;
#define VVIDEO_POLL_TABLE(_poll)	((_poll)->poll_wqueues.pt)
#else
	poll_table poll_table;
#define VVIDEO_POLL_TABLE(_poll)	(_poll)->poll_table
	unsigned int index;		/* ctx->poll.spinlock */
	struct vvideo_poll_table_entry_t entries[VVIDEO_POLL_MAX];
#endif
	unsigned int cookie;
	int wakeups;			/* ctx->poll.spinlock */
	unsigned int revents;		/* ctx->poll.spinlock */
	struct list_head ctx_list;	/* ctx->poll.spinlock */
	struct list_head link_list;	/* vl->events.spinlock */
	struct kref kref;
};

struct vvideo_ctx_t {
	/* Fields set at init time once for all */
	struct vvideo_dev_t *dev;
	unsigned int be_context;
	char name[32];
#ifdef VVIDEO_USE_FD
	int fd;
#else
	struct file *file;
#endif
	/* Work variables */
	struct vvideo_dmabuf_t dma_bufs[VVIDEO_DMABUF_MAX];
	unsigned int dma_buf_last;
	int streamon[VVIDEO_BUF_TYPE_MAX]; /* For information only */
	struct kref kref_busy; /* vvideo_dev_t::lock */
	struct kref kref_used; /* vvideo_dev_t::lock */
	enum vvideo_ctx_state_t state;
	unsigned int streamoff_pending; /* vvideo_dev_t::lock */
	struct vvideo_msg_t *async_queue; /* vvideo_dev_t::lock */
	wait_queue_head_t async_wait;
	struct {
		spinlock_t spinlock;
		/* Entries in this list take vvideo_ctx_t::kref_used */
		struct list_head list_head; /* poll::spinlock */
	} poll;
	/* Deferred kref_put() performed from poll interrupt handler */
	struct {
		struct list_head list_head;
		int counter;
	} kref_put;
	struct vvideo_msg_t *release_msgs; /* vvideo_dev_t::lock */
#ifdef VVIDEO_DQBUF
	unsigned long qbuf[VVIDEO_BUF_TYPE_MAX]; /* vvideo_dev_t::lock */
#endif
#ifdef VVIDEO_DPBS
	struct {
		int fd;
		struct dma_buf *dma_buf;
		struct mfc_dec_dpb_ref_info *info;
	} dpbs;
#endif
};

struct vvideo_link_t {
	/* Fields set at init time once for all */
	struct vvideo_be_t *be; /* Parent */
	struct vvideo_configs_t configs; /* Configured devices */
	unsigned int dev_width; /* Bits for minor in be_context */
	unsigned int dev_mask; /* Mask for minor in be_context */
	vmq_link_t *link; /* Inter-VM communications */
	unsigned int number; /* Link number */
	char name[32]; /* For messages */
	vmq_xx_config_t xx_config; /* Receive side */
	struct vvideo_dev_t *devs; /* Array of device descriptors */
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_connection *vfence2_connection;
#endif
	struct vvideo_msg_t *msgs;
	/* Work variables */
	_Bool connected; /* Callback link_on received */
	_Bool receive_notify; /* Callback received */
	struct mutex lock; /* Serializes msg reception */
#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
	u32 fe_version;
	u32 be_version;
#endif
	struct {
		spinlock_t spinlock;
		/* Entries in this list take vvideo_ctx_t::kref_used */
		struct list_head list_head; /* events::spinlock */
	} events;
#ifdef VVIDEO_REMEMBER_TRANSACTIONS
	unsigned int next_transaction;
	unsigned int transactions[VVIDEO_LAST_TRANSACTIONS];
#endif
};

struct vvideo_msg_t {
	/* Fields set once for all */
	struct vvideo_link_t *vl;
	struct VVideo2Request *req;
	/* Work variables */
	unsigned short req_num;
	unsigned short req_flags;
	unsigned int be_context;
	struct vvideo_dev_t *dev;
	struct vvideo_ctx_t *ctx;
	union {
		struct v4l2_buffer b;
		struct v4l2_edid edid;
	} secure;
	struct vvideo_msg_t *next;
};

struct vvideo_thread_t {
	/* Fields set at init time once for all */
	struct vvideo_be_t *be; /* Parent */
	char name[32];
	/* Work variables */
	_Bool aborted; /* vvideo_thread_t::lock */
	struct completion completion;
	pid_t pid;
	struct pid *struct_pid;
	struct mutex lock;
	_Bool working; /* vvideo_thread_t::lock */
	unsigned int deferred_pending;
	struct vvideo_msg_t *m;
	/* Fields which used to be ioctl locals */
	struct {
		unsigned int cmd;
		unsigned long arg;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		int fence_fd;
#endif
	} ioctl;
};

struct vvideo_dev_t {
	/* Fields set at init time once for all */
	struct vvideo_link_t *link; /* Parent */
	unsigned int minor;
	char name[32];
	/* Work variables */
	_Bool reset_pending;
	struct kref kref; /* vvideo_dev_t::lock */
	/* Protects last_be_context and DMABUF routines */
	struct mutex lock;
	struct vvideo_ctx_t ctx[VVIDEO_CTX_MAX];
	unsigned int last_be_context;
	struct vdev_errors {
		unsigned int gid_invalid;
		unsigned int out_of_slots;
		unsigned int dmabuf_import;
		unsigned int dma_buf_fd;
		unsigned int dma_buf_get;
		unsigned int dmabuf_export;
		unsigned int fd_negative;
		unsigned int map_fd_back;
		unsigned int not_dmabuf;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		unsigned int import_fence;
#endif
		unsigned int b_length;
		unsigned int open_ebusy;
		unsigned int no_private_data;
		unsigned int req_unknown;
		unsigned int too_many_contexts;
		unsigned int could_not_open;
		unsigned int could_not_read;
		unsigned int name_mismatch;
		unsigned int find_node;
#ifdef VVIDEO_USE_FD
		unsigned int sys_open;
#else
		unsigned int filp_open;
#endif
		unsigned int invalid_context;
		unsigned int release_invalid;
		unsigned int poll_error;
		unsigned int edid_blocks;
		unsigned int p_count;
		unsigned int ext_gid;
#ifdef VVIDEO_DPBS
		struct {
			unsigned int multiple;
			unsigned int shared_get;
			unsigned int vmap;
		} dpbs;
#endif
	} errors;
	struct vdev_warnings {
		unsigned int f_count;
		unsigned int sys_close;
#ifndef VVIDEO_USE_FD
		unsigned int filp_close;
#endif
		unsigned int multiple_release_fd;
#ifdef VVIDEO_ASSERTS
		unsigned int failed_assert;
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		unsigned int export_fence;
#endif
		struct {
			unsigned int cookie;
			unsigned int dup_wait;
			unsigned int f_op;
			unsigned int max;
			unsigned int too_many;
		} poll;
		unsigned int kmem_cache_alloc;
#ifdef VVIDEO_DQBUF
		unsigned int dup_qbuf;
		unsigned int dup_dqbuf;
#endif
#ifdef VVIDEO_DPBS
		struct {
			unsigned int index;
			unsigned int size;
		} dpbs;
#endif
	} warnings;
	struct vdev_stats {
		unsigned int fc_down;
		unsigned int ctx_wrap;
		unsigned int ctx_clash;
		unsigned int deferred_returns;
		unsigned int behind_async;
		unsigned int wait_async;
		unsigned int wait_signal;
		unsigned int next_none;
		unsigned int next_async;
		unsigned int next_sync;
		unsigned int gid2fd_0;
		unsigned int gid2fd_n;
		unsigned int gid2fd_alloc;
		struct {
			unsigned int discard; /* At ctx close */
			unsigned int dup; /* Useless callbacks from driver */
			unsigned int eagain; /* Ran out of VMQ msgs */
			unsigned int event; /* Sent to frontend */
			unsigned int key_null; /* Queue wakeup with key 0 */
			unsigned int key_set; /* Queue wakeup with key not 0 */
			unsigned int mask; /* req_poll returns non zero */
			unsigned int no_event; /* key brings no valid event */
			unsigned int no_list; /* req_poll not on wait list */
			unsigned int no_wait; /* First poll, does not wait */
			unsigned int second; /* Second poll */
			unsigned int wait; /* First poll, waits */
			unsigned int wakeup; /* Original wake-ups from driver */
		} poll;
		unsigned int fences_ok;
		unsigned int fences_removed;
		unsigned int stale_abort;
		unsigned int stale_ctx;
		unsigned int release_msg;
		unsigned int release_sent;
		unsigned int flag_error;
		unsigned int deleted_fd;
#ifdef VVIDEO_DPBS
		struct {
			unsigned int buffer;
			unsigned int dqbuf;
			unsigned int export;
			unsigned int plane;
			unsigned int repeat;
			unsigned int s_ctrl;
			unsigned int zero;
		} dpbs;
#endif
	} stats;
};

/* One per driver */
struct vvideo_be_t {
	/* Fields set at init time once for all */
#ifdef CONFIG_PROC_FS
	struct vvideo_be_t *proc_normal; /* Must be first */
	struct vvideo_be_t *proc_ext;
#endif
	const char *name;
	vmq_links_t *links;
	unsigned int link_count;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_client *vfence2_client;
#endif
#ifndef VVIDEO_LOCAL_GID
	struct vdmaheap_client *vdmaheap_client;
#endif
	struct vvideo_thread_t *threads;
	unsigned int max_reqs; /* Entries in be->threads */
#ifdef CONFIG_PROC_FS
	_Bool proc_exists; /* /proc/nk/VVIDEO_DRIVER has been created */
#endif
#if KERNEL_VERSION(4, 17, 0) <= LINUX_VERSION_CODE
	int (*sys_unshare)(unsigned long unshare_flags);
#else
	long (*sys_unshare)(unsigned long unshare_flags);
#endif
	pid_t (*kernel_thread)(int (*fn)(void *), void *arg,
			       unsigned long flags);
#ifdef VVIDEO_USE_FD
	long (*sys_open)(const char __user *filename, int flags, umode_t mode);
#if KERNEL_VERSION(4, 17, 0) <= LINUX_VERSION_CODE
	int (*sys_ioctl)(unsigned int fd, unsigned int cmd, unsigned long arg);
#else
	long (*sys_ioctl)(unsigned int fd, unsigned int cmd, unsigned long arg);
#endif
	long (*sys_read)(unsigned int fd, char __user *buf, size_t count);
#endif
	int fd_zero;
	/* Work variables */
	_Bool sysconf_notify; /* Callback received */
	_Bool receive_notify; /* Callback received */
	struct mutex lock; /* Protects thread table */
	struct kref idle_threads; /* Not in V4L2 ops; vvideo_be_t::lock */
	struct kref running_threads; /* vvideo_be_t::lock */
	struct kref open_ctxs; /* vvideo_be_t::lock */
	wait_queue_head_t wait;
	struct kmem_cache *poll_kmem_cache;
	struct {
		_Bool notify;
		spinlock_t spinlock;
		struct list_head list_head;
	} ctx_put;
	atomic_t ctx_close_notify; /* Deferred kref_put(be->open_ctxs) */
	struct {
#ifdef VVIDEO_ASSERTS
		unsigned int failed_assert;
#endif
	} warnings;
	struct {
		unsigned int thread_starts;
		unsigned int kills;
		unsigned int stopped_kills;
		unsigned int max_threads;
		unsigned int kernel_thread;
		unsigned int max_running;
		unsigned int invalid_dev;
		unsigned int sig_processed;
		unsigned int sig_delivered;
		unsigned int kill_retry;
	} stats;
};

struct vvideo_call_t {
	unsigned int cmd;
	int (*before)(struct vvideo_thread_t *t);
	int (*after)(struct vvideo_thread_t *t, int ret);
};

struct vvideo_v4l2_exportbuffer {
	struct v4l2_exportbuffer eb;
	struct vvideo_dmabuf_gid_t gid;
};

/*----- Helper macros -----*/

#ifdef VVIDEO_ASSERTS
#define VVIDEO_ASSERT(_obj, _x)                                                \
	do {                                                                   \
		if (!(_x)) {                                                   \
			VVIDEO_WARN_LIMITED(                                   \
				(_obj)->warnings.failed_assert,                \
				"object %s Assert failed %s line %d\n",        \
				(_obj)->name, #_x, __LINE__);                  \
		}                                                              \
	} while (0)
#else
#define VVIDEO_ASSERT(_obj, _x)                                                \
	do {                                                                   \
	} while (0)
#endif

#define VVIDEO_ASSERT_BE(x) VVIDEO_ASSERT(be, x)

#define VVIDEO_FOR_ALL_CTXS(_vvideo_dev, _i, _ctx)                             \
	for ((_i) = 0, (_ctx) = (_vvideo_dev)->ctx; (_i) < VVIDEO_CTX_MAX;     \
	     ++(_i), ++(_ctx))

#define VVIDEO_FOR_ALL_THREADS(_be, _i, _t)                                    \
	for ((_i) = 0, (_t) = (_be)->threads; (_i) < (_be)->max_reqs;          \
	     ++(_i), ++(_t))

/*----- Globals -----*/

static const char *const vvideo_ctx_state_name[] = {
	[VVIDEO_CS_FREE] = "FREE",
	[VVIDEO_CS_BEING_OPENED] = "BEING_OPENED",
	[VVIDEO_CS_OPEN] = "OPEN",
	[VVIDEO_CS_CLOSE_PENDING] = "CLOSE_PENDING",
	[VVIDEO_CS_BEING_CLOSED] = "BEING_CLOSED"
};

/*----- Common code -----*/

#define VVIDEO_LINK(link) (*(struct vvideo_link_t **)(link))
#define VVIDEO_LINKS_BE(links) (*(struct vvideo_be_t **)(links))

#include "vvideo2-common.c"

/*----- kref helpers -----*/

/*
 * These helpers are only used with krefs which are associated
 * with mutexes (kref_put_mutex), in situations where going down
 * to 0 and back up is expected and cyclic.
 * Such krefs must be always incremented while mutex is held,
 * to serialize re-use with release handler after last use
 * (which operates while counter is already zero).
 * The sole benefit versus plain counters is that decrementing
 * only takes a mutex at 1 to 0 transition.
 * With CONFIG_REFCOUNT_FULL, kref_get() fails if counter
 * is zero; kref_init() must be used instead. Rather than having
 * a dedicated boolean saying "init required" and tested in the
 * locked section, we use the counter value itself.
 * In some cases whether to kref_init() or to kref_get() is known
 * at compilation time, but we do not rely on this.
 * Associated mutex must be already locked.
 */

static _Bool vvideo_kref_get_unlocked(struct kref *kref)
{
	const _Bool new = vvideo_kref_read(kref) == 0;

	if (new)
		kref_init(kref);
	else
		kref_get(kref);
	return new;
}

/*
 * Mutex must be Unlocked
 */

static VVIDEO_NOINLINE void vvideo_kref_get_mutex(struct kref *kref,
						  struct mutex *lock)
{
	mutex_lock(lock);
	vvideo_kref_get_unlocked(kref);
	mutex_unlock(lock);
}

/*----- Version helpers -----*/

static inline struct list_head *vvideo_wait_queue_entry_list(
					const VVIDEO_WAIT_QUEUE_ENTRY *entry)
{
#if KERNEL_VERSION(4, 12, 0) > LINUX_VERSION_CODE
	return (struct list_head *) &entry->task_list;
#else
	return (struct list_head *) &entry->entry;
#endif
}

/*----- DMABUF code -----*/

#define VVIDEO_ZERO(_ptr) memset((_ptr), 0, sizeof(*(_ptr)))
#define VVIDEO_FD_VALID(fd) ((fd) >= 0)

static int vvideo_dmabuf_export(struct vvideo_ctx_t *ctx,
				struct dma_buf *const dma_buf,
				struct vvideo_dmabuf_gid_t *gid)
{
#ifdef VVIDEO_LOCAL_GID
	gid->dma_buf = (unsigned long)dma_buf;
	/* Increase dma_buf reference count */
	get_dma_buf(dma_buf);
	return 0;
#else
	/* Increases dma_buf reference count */
	return vdmaheap_kapi_export(ctx->dev->link->be->vdmaheap_client, dma_buf,
				&gid->gid, &gid->cred);
#endif
}

/* vvideo_dev_t::lock must be Locked on entry */

static int vvideo_dmabuf_import(struct vvideo_ctx_t *ctx,
				const struct vvideo_dmabuf_gid_t *gid,
				struct dma_buf **dma_buf)
{
#ifdef VVIDEO_LOCAL_GID
	*dma_buf = (struct dma_buf *)gid->dma_buf;
	get_dma_buf(*dma_buf);
	return 0;
#else
	char buf[VVIDEO_GID_STRING];
	int ret;

	/* Increases dma_buf reference count */
	ret = vdmaheap_kapi_import(ctx->dev->link->be->vdmaheap_client, gid->gid,
				&gid->cred, dma_buf);
	if (ret < 0)
		VVIDEO_ERR_LIMITED(ctx->dev->errors.dmabuf_import,
			"ctx %s vdmaheap_kapi_import(%s) failed (%d)\n",
			ctx->name, vvideo_dmabuf_gid_format(gid, buf,
			sizeof(buf)), ret);
	return ret;
#endif
}

/*
 * vvideo_dev_t::lock must be Locked on entry
 * Initial kref is 2.
 * Reference #1 allows to hold struct vvideo_dmabuf_t entries
 * in struct vvideo_ctx_t::dma_bufs table between VIDIOC_QBUF and
 * matching VIDIOC_DQBUF ioctls.
 * Reference #2 is for current thread, so that the mapping
 * entry remains in table even if another thread performs
 * VIDIOC_DQBUF after vvideo_dev_t::lock has been released
 * for the ioctl, and removes reference #1. Indeed, current
 * thread still needs the entry on ioctl return, to map back
 * into gids whatever the QBUF ioctl has left in v4l2_buffer.
 */

static inline void
vvideo_dmabuf_init(struct vvideo_dmabuf_t *d, struct vvideo_ctx_t *ctx,
		   const struct vvideo_dmabuf_gid_t *gid,
		   struct dma_buf *dma_buf, int fd, int buf_index, int plane,
		   enum v4l2_buf_type buf_type)
{
	d->ctx = ctx;
	d->gid = *gid;
	d->dma_buf = dma_buf;
	d->fd = fd;
	d->buf_index = buf_index;
	d->plane = plane;
	d->buf_type = buf_type;
	kref_init(&d->kref);
	kref_get(&d->kref);
	/* For information only */
	d->saved_f_count = file_count(dma_buf->file);
}

static void vvideo_dmabuf_reset(struct vvideo_dmabuf_t *d)
{
	VVIDEO_ZERO(d);
	d->fd = -1;
	d->buf_index = -1;
}

static _Bool vvideo_dmabuf_is_set(const struct vvideo_dmabuf_t *d)
{
	return d->dma_buf || VVIDEO_FD_VALID(d->fd) || d->buf_index >= 0 ||
	       d->buf_type || vvideo_kref_read(&d->kref);
}

#define VVIDEO_DMABUF_FORMAT	160

static const char *const vvideo_dmabuf_legend =
	"V DMABUF-address-- FD- RC SF Nd/P Ty TypeName GID";

static char *vvideo_dmabuf_format(const struct vvideo_dmabuf_t *d, char *buf,
				  size_t bufsize)
{
	int len = snprintf(buf, bufsize, "%1d %16p %3d %2d %2d %2d/%d %2d %s ",
		d->valid, d->dma_buf, d->fd,
		(int)vvideo_kref_read(&d->kref), d->saved_f_count,
		d->buf_index, d->plane, d->buf_type, d->buf_type ?
		vvideo_v4l2_buf_type_short(d->buf_type) : "NONE");
	if ((unsigned int) len < bufsize)
		vvideo_dmabuf_gid_format(&d->gid, buf + len, bufsize - len);
	return buf;
}

/*
 * We increase kref +2, because kernel is now
 * having a reference, and because we need to hold
 * entry in table until ioctl returns, to be able
 * to remap back.
 * Called on VIDIOC_QBUF, VIDIOC_PREPARE_BUF and selected VIDIOC_S_CTRL.
 * We check flags and increase counters and kref accordingly.
 * vvideo_dev_t::lock must be Unlocked on entry.
 */

static int vvideo_dmabuf_gid2fd(struct vvideo_ctx_t *ctx,
				const struct vvideo_dmabuf_gid_t *gid,
				int buf_index, int plane,
				enum v4l2_buf_type buf_type, unsigned int flags)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_dmabuf_t *last_free = NULL;
	struct dma_buf *dma_buf;
	struct vvideo_dmabuf_t *d;
	int d_counter;
	int fd;
	char buf[max(VVIDEO_DMABUF_FORMAT, VVIDEO_GID_STRING)];
	int ret;

	/* Invalid gid could match unused slots in array, so refuse it here */
	if (!VVIDEO_GID_VALID(gid)) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.gid_invalid,
				   "ctx %s gid %s invalid\n", ctx->name,
				   vvideo_dmabuf_gid_format(gid, buf,
							    sizeof(buf)));
		return -EINVAL;
	}
	mutex_lock(&vvideo_dev->lock);
	VVIDEO_FOR_ALL_DMABUFS_REVERSE(ctx, d)
	{
		if (!d->valid) {
			last_free = d;
			continue;
		}
		if (!vvideo_dmabuf_gid_same(&d->gid, gid))
			continue;
		VVIDEO_LOG("ctx %s Already got %s\n", ctx->name,
			vvideo_dmabuf_format(d, buf, sizeof(buf)));
		VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&d->kref) >= 1);
		if (flags & VVIDEO_DMABUF_PREPARED) {
			VVIDEO_ASSERT(vvideo_dev,
				!(flags & VVIDEO_DMABUF_QUEUED));
			/* PREPARE_BUF only makes sense before QBUF */
			/* A second one is ignored */
		}
		if (flags & VVIDEO_DMABUF_QUEUED) {
			VVIDEO_ASSERT(vvideo_dev,
				!(flags & VVIDEO_DMABUF_PREPARED));
			++d->queued;
			if (d->prepared)
				d->prepared = false;
			else {
				if (vvideo_kref_get_unlocked(&d->kref))
					++vvideo_dev->stats.gid2fd_0;
				else
					++vvideo_dev->stats.gid2fd_n;
			}
		}
		if (flags & VVIDEO_DMABUF_RUNNING) {
			++d->running;
			if (vvideo_kref_get_unlocked(&d->kref))
				++vvideo_dev->stats.gid2fd_0;
			else
				++vvideo_dev->stats.gid2fd_n;
		}
		mutex_unlock(&vvideo_dev->lock);
		return d->fd;
	}
	d = last_free;	/* Using last_free would be confusing */
	if (!d) {
		mutex_unlock(&vvideo_dev->lock);
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.out_of_slots,
			"ctx %s out of slots for gid %s\n", ctx->name,
			vvideo_dmabuf_gid_format(gid, buf, sizeof(buf)));
		return -ENOMEM;
	}
	/* Importing increases dma_buf reference count */
	ret = vvideo_dmabuf_import(ctx, gid, &dma_buf);
	if (ret) {
		/* Error message already issued */
		mutex_unlock(&vvideo_dev->lock);
		return ret;
	}
	VVIDEO_ASSERT(vvideo_dev, file_count(dma_buf->file) >= 1);

	/* dma_buf_fd() alone does not increase the f_count */
	fd = dma_buf_fd(dma_buf, O_CLOEXEC | O_RDWR);
	VVIDEO_ASSERT(vvideo_dev, file_count(dma_buf->file) >= 1);
	if (!VVIDEO_FD_VALID(fd)) {
		mutex_unlock(&vvideo_dev->lock);
		dma_buf_put(dma_buf); /* void */
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dma_buf_fd,
			"ctx %s dma_buf_fd(%p) failed (%d)\n",
			ctx->name, dma_buf, fd);
		return fd;
	}
	vvideo_dmabuf_init(d, ctx, gid, dma_buf, fd, buf_index, plane,
		buf_type);
	if (flags & VVIDEO_DMABUF_PREPARED) {
		VVIDEO_ASSERT(vvideo_dev, !(flags & VVIDEO_DMABUF_QUEUED));
		d->prepared = true;
	}
	if (flags & VVIDEO_DMABUF_QUEUED) {
		VVIDEO_ASSERT(vvideo_dev, !(flags & VVIDEO_DMABUF_PREPARED));
		d->queued = 1;
	}
	if (flags & VVIDEO_DMABUF_RUNNING)
		d->running = 1;
	++vvideo_dev->stats.gid2fd_alloc;
	d->valid = true;
	ctx->dma_buf_last = d - ctx->dma_bufs;
	mutex_unlock(&vvideo_dev->lock);
	VVIDEO_LOG_DMABUF("ctx %s Mapped %s to fd %d flags %#x\n", ctx->name,
		vvideo_dmabuf_format(d, buf, sizeof(buf)), fd, flags);
	return fd;
}

static int vvideo_sys_close(const int fd)
{
#if KERNEL_VERSION(4, 16, 0) <= LINUX_VERSION_CODE
	return ksys_close(fd);
#else
	return sys_close(fd);
#endif
}

static void vvideo_ctx_close_fd(struct vvideo_ctx_t *ctx, const int fd)
{
	const int ret = vvideo_sys_close(fd);

	if (ret)
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.sys_close,
				    "ctx %s ksys_close(%d) failed (%d)\n",
				    ctx->name, fd, ret);
}

/* vvideo_dev_t::lock must be Locked on entry */

static noinline void vvideo_dmabuf_kref_release(struct kref *kref)
{
	struct vvideo_dmabuf_t *d =
			container_of(kref, struct vvideo_dmabuf_t, kref);
	struct vvideo_ctx_t *ctx = d->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	const int f_count = file_count(d->dma_buf->file);
	char buf[max(VVIDEO_DMABUF_FORMAT, VVIDEO_GID_STRING)];

	(void)buf;
	VVIDEO_LOG_DMABUF("ctx %s Release %s\n", ctx->name,
		vvideo_dmabuf_format(d, buf, sizeof(buf)));
	VVIDEO_ASSERT(vvideo_dev, f_count >= 1);
	/*
	 * We should have same kernel f_count as originally,
	 * except if V4L2 is still holding a reference to buffer,
	 * and except if buffer was shared with another context
	 * (e.g. Scaler output buffer being also an MFC capture
	 * buffer) and this other context has released it since.
	 * Popular situations are 4 to 3, 3 to 2, 4 to 2, 3 to 1.
	 */
	if (f_count < d->saved_f_count)
		++vvideo_dev->stats.fc_down;
	/* We must close the fd, dma_buf_put() alone will not do that */
	vvideo_ctx_close_fd(ctx, d->fd);
	/* Do not access d->dma_buf anymore because it is freed */
	d->valid = false;
}

/*
 * Release references specified by flags, either for one thread
 * or for all of them.
 * vvideo_dev_t::lock must be Locked on entry.
 */
static noinline void vvideo_dmabuf_release_flags(struct vvideo_dmabuf_t *d,
					const unsigned int flags,
					const _Bool one)
{
	struct vvideo_dev_t *vvideo_dev = d->ctx->dev;
	char buf[VVIDEO_GID_STRING];

	(void) buf;
	VVIDEO_LOG_DMABUF(
		"gid %s dma_buf %p fd %d kref %d f_count %d %s flags %#x %d\n",
		vvideo_dmabuf_gid_format(&d->gid, buf, sizeof(buf)),
		d->dma_buf, d->fd, (int)vvideo_kref_read(&d->kref),
		(int) file_count(d->dma_buf->file),
		vvideo_v4l2_buf_type_short(d->buf_type), flags, one);
	VVIDEO_LOG_DMABUF("prepared %d queued %d running %d\n",
		d->prepared, d->queued, d->running);

	(void) vvideo_dev;
	VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&d->kref) > 0);
	/*
	 * kref_put() returns 1 when we reached zero
	 * and the handler has been executed.
	 */
	if (flags & VVIDEO_DMABUF_PREPARED) {
		if (d->prepared) {
			d->prepared = false;
			VVIDEO_ASSERT(vvideo_dev,
				vvideo_kref_read(&d->kref) > 0);
			if (kref_put(&d->kref, vvideo_dmabuf_kref_release))
				return;
		}
	}
	if (flags & VVIDEO_DMABUF_QUEUED) {
		while (d->queued > 0) {
			--d->queued;
			VVIDEO_ASSERT(vvideo_dev,
				vvideo_kref_read(&d->kref) > 0);
			if (kref_put(&d->kref, vvideo_dmabuf_kref_release))
				return;
			if (one)
				break;
		}
	}
	if (flags & VVIDEO_DMABUF_RUNNING) {
		while (d->running > 0) {
			--d->running;
			VVIDEO_ASSERT(vvideo_dev,
				vvideo_kref_read(&d->kref) > 0);
			if (kref_put(&d->kref, vvideo_dmabuf_kref_release))
				return;
			if (one)
				break;
		}
	}
}

/*
 * Remove a "kernel" reference to "fd" in case of DQBUF.
 * Remove a "caller" reference to "fd" in case of Okay QBUF.
 * Remove both references to "fd" in case of Failed QBUF.
 * Called on return from VIDIOC_QBUF, VIDIOC_DQBUF, VIDIOC_PREPARE_BUF
 * and selected VIDIOC_G_CTRL and VIDIOC_S_CTRL.
 * vvideo_dev_t::lock must be Unlocked.
 */

static int vvideo_dmabuf_fd2gid(struct vvideo_ctx_t *ctx, const int fd,
				struct vvideo_dmabuf_gid_t *gid,
				const unsigned int flags,
				const unsigned int cmd)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_dmabuf_t *deleted = NULL;
	char buf[VVIDEO_GID_STRING];
	struct vvideo_dmabuf_t *d;
	int d_counter;

	if (!VVIDEO_FD_VALID(fd)) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.fd_negative,
				   "ctx %s %s fd %d negative\n", ctx->name,
				   vvideo_ioctl_name(cmd), fd);
		return -EINVAL;
	}
	mutex_lock(&vvideo_dev->lock);
	VVIDEO_FOR_ALL_DMABUFS_REVERSE(ctx, d)
	{
		if (d->fd != fd)
			continue;
		if (!d->valid) {
			if (!(flags & VVIDEO_DMABUF_DELETED))
				continue;
			if (!deleted)
				deleted = d;
			continue;
		}
		*gid = d->gid;
		(void)buf;
		VVIDEO_LOG_DMABUF(
	"ctx %s %s Mapped fd %d to gid %s kref %d f_count %d flags %#x\n",
			ctx->name, vvideo_ioctl_name(cmd), fd,
			vvideo_dmabuf_gid_format(&d->gid, buf, sizeof(buf)),
			(int)vvideo_kref_read(&d->kref),
			(int)file_count(d->dma_buf->file), flags);
		vvideo_dmabuf_release_flags(d, flags, true);
		mutex_unlock(&vvideo_dev->lock);
		return 0;
	}
	if (deleted) {
		d = deleted;
		/* Only access safe fields */
		*gid = d->gid;
		(void) buf;
		VVIDEO_LOG_DMABUF("ctx %s %s deleted fd %d gid %s flags %#x\n",
			ctx->name, vvideo_ioctl_name(cmd), fd,
			vvideo_dmabuf_gid_format(&d->gid, buf, sizeof(buf)),
			flags);
#ifdef VVIDEO_DQBUF
		VVIDEO_LOG_DMABUF(
		    "ctx %s index %d plane %d type %d streamon %d qbuf %lx\n",
			ctx->name, d->buf_index, d->plane, d->buf_type,
			ctx->streamon[d->buf_type], ctx->qbuf[d->buf_type]);
#else
		VVIDEO_LOG_DMABUF(
			"ctx %s index %d plane %d type %d streamon %d\n",
			ctx->name, d->buf_index, d->plane, d->buf_type,
			ctx->streamon[d->buf_type]);
#endif
		mutex_unlock(&vvideo_dev->lock);
		++vvideo_dev->stats.deleted_fd;
		return 0;
	}
	mutex_unlock(&vvideo_dev->lock);
	VVIDEO_ERR_LIMITED(vvideo_dev->errors.map_fd_back,
		"ctx %s %s could not map fd %d back\n", ctx->name,
		vvideo_ioctl_name(cmd), fd);
	return -ENOENT;
}

/*
 * Removes references to "fd" in case of failed VIDIOC_QBUF
 * or VIDIOC_PREPARE_BUF preparation.
 * vvideo_dev_t::lock must be Unlocked on entry
 */

static void vvideo_dmabuf_release_fd(struct vvideo_ctx_t *ctx, const int fd,
					unsigned int flags)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	int count = 0;
	struct vvideo_dmabuf_t *d;
	int d_slot;

	mutex_lock(&vvideo_dev->lock);
	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		if (!d->valid)
			continue;
		if (d->fd != fd)
			continue;
		vvideo_dmabuf_release_flags(d, flags, true);
		++count;
	}
	mutex_unlock(&vvideo_dev->lock);
	if (count > 1)
		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.multiple_release_fd,
			"ctx %s Closed %d fds %d\n", ctx->name, count, fd);
}

/*
 * On STREAMOFF, which applies to one v4l2_buf_type only.
 * Called only after all DQBUF have finished running.
 * Only release PREPARED and QUEUED references.
 * vvideo_dev_t::lock must be Locked.
 */

static void vvideo_dmabuf_release_buf_type(struct vvideo_ctx_t *ctx,
					   const enum v4l2_buf_type buf_type)
{
	struct vvideo_dmabuf_t *d;
	int d_slot;

	VVIDEO_LOG_DMABUF("ctx %s buf_type %d (%s)\n", ctx->name, buf_type,
			  vvideo_v4l2_buf_type_short(buf_type));
	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		if (!d->valid)
			continue;
		if (d->buf_type != buf_type)
			continue;
		vvideo_dmabuf_release_flags(d,
			VVIDEO_DMABUF_PREPARED | VVIDEO_DMABUF_QUEUED, false);
	}
}

/*
 * When context is closed, explicitly or on loss of connection.
 * Called from: vvideo_ctx_free() only.
 * vvideo_dev_t::lock must be Locked on entry
 */

static void vvideo_dmabuf_release_all(struct vvideo_ctx_t *ctx)
{
	struct vvideo_dmabuf_t *d;
	int d_slot;

	VVIDEO_LOG_DMABUF("ctx %s\n", ctx->name);
	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		if (!d->valid)
			continue;
		vvideo_dmabuf_release_flags(d, VVIDEO_DMABUF_ALL, false);
	}
}

/*----- Context management -----*/

/*
 * Called at module init time, from vvideo_dev_init().
 * Called from vvideo_ctx_free().
 */

static void vvideo_ctx_init(struct vvideo_ctx_t *ctx,
			    struct vvideo_dev_t *vvideo_dev)
{
	struct vvideo_dmabuf_t *d;
	int d_slot;
	enum v4l2_buf_type buf_type;

	VVIDEO_ZERO(ctx);
	ctx->dev = vvideo_dev;
	/* be_context inited by vvideo_ctx_alloc() */
	/* name inited by vvideo_ctx_alloc() */
	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		vvideo_dmabuf_reset(d);
	}
	ctx->dma_buf_last = VVIDEO_DMABUF_MAX - 1;
	for (buf_type = 0; buf_type < VVIDEO_BUF_TYPE_MAX; ++buf_type)
		ctx->streamon[buf_type] = -1;
#ifdef VVIDEO_USE_FD
	ctx->fd = -1;
#endif
	/* Leave file at 0 */
	/* Leave kref_busy at 0 */
	/* Leave kref_used at 0 */
	ctx->state = VVIDEO_CS_FREE;
	/* Leave streamoff_pending at 0 */
	/* Leave async_queue at NULL */
	init_waitqueue_head(&ctx->async_wait);
	spin_lock_init(&ctx->poll.spinlock);
	INIT_LIST_HEAD(&ctx->poll.list_head);
	INIT_LIST_HEAD(&ctx->kref_put.list_head);
	/* Leave ctx->kref_put.counter at 0 */
	/* Leave ctx->release_msgs at NULL */
	/* Leave ctx->qbuf at 0 */
}

#define VVIDEO_CTX_FILE_FORMAT_MAX 32

static inline const char *vvideo_ctx_file_format(const struct vvideo_ctx_t *ctx,
					  char *buf,
					  size_t bufsize)
{
#ifdef VVIDEO_USE_FD
	snprintf(buf, bufsize, "%d", ctx->fd);
#else
	snprintf(buf, bufsize, "%p", ctx->file);
#endif
	return buf;
}

/* vvideo_dev_t::lock must be Locked */

static inline unsigned int vvideo_dev_alloc_be_context(
						struct vvideo_dev_t *vvideo_dev)
{
	/* "<<" has higher precedence than "|" */
	return vvideo_dev->minor | ++vvideo_dev->last_be_context
					   << vvideo_dev->link->dev_width;
}

/*
 * Called at each VVIDEO2_REQ_OPEN request
 * from vvideo_req_open() only.
 * vvideo_dev_t::lock must be Unlocked.
 */

static struct vvideo_ctx_t *vvideo_ctx_alloc(struct vvideo_dev_t *vvideo_dev)
{
	struct vvideo_ctx_t *ctx;
	int ctx_slot;

	mutex_lock(&vvideo_dev->lock);
	VVIDEO_FOR_ALL_CTXS(vvideo_dev, ctx_slot, ctx)
	{
		struct vvideo_ctx_t *ctx2;
		int ctx2_slot;

		if (ctx->be_context)
			continue;
		do {
			ctx->be_context =
				vvideo_dev_alloc_be_context(vvideo_dev);
			/* Make sure be_context is not already used */
			VVIDEO_FOR_ALL_CTXS(vvideo_dev, ctx2_slot, ctx2)
			{
				if (ctx == ctx2)
					continue;
				if (!ctx->be_context) {
					++vvideo_dev->stats.ctx_wrap;
					break;
				}
				if (ctx->be_context == ctx2->be_context) {
					++vvideo_dev->stats.ctx_clash;
					break;
				}
			}
		} while (ctx2_slot < VVIDEO_CTX_MAX);
		snprintf(ctx->name, sizeof(ctx->name), "%d.%d.%d.%x",
			 vvideo_dev->link->number, vvideo_dev->minor,
			 ctx_slot, ctx->be_context);
		kref_init(&ctx->kref_busy); /* 1 for current thread */
		kref_init(&ctx->kref_used); /* 1 for current thread */
		ctx->state = VVIDEO_CS_BEING_OPENED;
		mutex_unlock(&vvideo_dev->lock);
		return ctx;
	}
	mutex_unlock(&vvideo_dev->lock);
	return NULL;
}

#ifdef VVIDEO_DPBS
static void vvideo_ctx_dpbs_release(struct vvideo_ctx_t *ctx)
{
	if (ctx->dpbs.info) {
		dma_buf_vunmap(ctx->dpbs.dma_buf, ctx->dpbs.info); /* void */
		ctx->dpbs.info = NULL;
	}
	if (ctx->dpbs.dma_buf) {
		dma_buf_put(ctx->dpbs.dma_buf); /* void */
		ctx->dpbs.dma_buf = NULL;
	}
	ctx->dpbs.fd = -1;
}
#endif

/*
 * Called from:
 * - vvideo_ctx_close()
 * - vvideo_req_open(), on error only
 * There should be zero or one using threads.
 * After this call, the context slot can be reallocated.
 * There is no waiting for free slot, hence nobody to wake up.
 * vvideo_dev_t::lock must be Locked externally if necessary
 */

static void vvideo_ctx_free(struct vvideo_ctx_t *ctx)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;

	/* kref_busy is 0 on last put, and 1 on failed open */
	VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&ctx->kref_busy) == 0 ||
		      vvideo_kref_read(&ctx->kref_busy) == 1);
	VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&ctx->kref_used) == 0 ||
		      vvideo_kref_read(&ctx->kref_used) == 1);
#ifdef VVIDEO_DPBS
	vvideo_ctx_dpbs_release(ctx);
#endif
	vvideo_dmabuf_release_all(ctx);
	vvideo_ctx_init(ctx, vvideo_dev);
}

/*
 * Terminate all running threads except current one when
 * there are no more open contexts.
 * Called from vvideo_ctx_close() only.
 * vvideo_be_t::lock was Locked by caller.
 */

static VVIDEO_NOINLINE void vvideo_be_kref_open_ctxs_release(struct kref *kref)
{
	struct vvideo_be_t *be =
		container_of(kref, struct vvideo_be_t, open_ctxs);
	struct vvideo_thread_t *t;
	int t_slot;

	VVIDEO_LOG("entered, thread %d\n", current->pid);
	/* Check for module unload time */
	if (!be->threads)
		return;
	++be->stats.kills;
	/*
	 * Abort all running threads except ours.
	 * Wait for threads to terminate.
	 * Clean be->threads.
	 */
	VVIDEO_FOR_ALL_THREADS(be, t_slot, t)
	{
		if (t->pid == current->pid)
			continue;
		if (t->pid <= 0)
			continue;
		/*
		 * Killing t->working threads will lead
		 * to deadlock, as we hold be->lock, while
		 * these threads want to get it while being
		 * in working state before terminating.
		 * We can stop kills anyway, as the working
		 * thread will probably create another context.
		 */
		mutex_lock(&t->lock);
		if (t->working) {
			mutex_unlock(&t->lock);
			++be->stats.stopped_kills;
			VVIDEO_LOG("thread %s working, stopping kills\n",
				t->name);
			break;
		}
		t->aborted = true;
		mutex_unlock(&t->lock);

		wake_up_all(&be->wait);

		VVIDEO_LOG("pid %d waiting for thread %s\n",
			   current->pid, t->name);
		wait_for_completion(&t->completion);
		t->aborted = false;
		t->pid = 0;
		VVIDEO_LOG("%s completed, %d remain\n", t->name,
			   vvideo_kref_read(&be->running_threads));
		VVIDEO_ASSERT_BE(vvideo_kref_read(&be->running_threads) >=
			vvideo_kref_read(&be->idle_threads));
	}
	VVIDEO_LOG("exited\n");
}

/*
 * Called from:
 * - vvideo_ctx_kref_release() if a context has been logically closed
 * - vvideo_dev_release_ctxs()
 * vvideo_dev_t::lock should not be Locked, as this function can take time.
 * vvideo_be_t::lock can be Locked.
 */

static void vvideo_ctx_close(struct vvideo_ctx_t *ctx)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_be_t *be = vvideo_dev->link->be;
	char buf[VVIDEO_CTX_FILE_FORMAT_MAX];
	int ret;

	(void)buf;
	VVIDEO_LOG("ctx %s file %s kref_busy %d kref_used %d state %s\n",
		ctx->name, vvideo_ctx_file_format(ctx, buf, sizeof(buf)),
		vvideo_kref_read(&ctx->kref_busy),
		vvideo_kref_read(&ctx->kref_used),
		   vvideo_ctx_state_name[ctx->state]);
	VVIDEO_ASSERT(vvideo_dev, ctx->state == VVIDEO_CS_BEING_CLOSED);
#ifdef VVIDEO_USE_FD
	vvideo_ctx_close_fd(ctx, ctx->fd);
	ret = 0;
#else
	ret = filp_close(ctx->file, NULL);
	if (ret) {
		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.filp_close,
				    "ctx %s filp_close() failed (%d)\n",
				    ctx->name, ret);
		ret = 0;
	}
#endif
	VVIDEO_LOG("ctx %s be->open_ctxs %d\n", ctx->name,
		   vvideo_kref_read(&be->open_ctxs));
	/*
	 * Defer kref_put(be->open_ctxs) till thread exit.
	 * We used to "kref_put_mutex(&be->open_ctxs,
	 * vvideo_be_kref_open_ctxs_release, &be->lock)" here
	 * but now we can come with be->lock taken.
	 */
	atomic_inc(&be->ctx_close_notify);
#ifdef VVIDEO_MSLEEP_CLOSE
	msleep(1000);
#endif
	VVIDEO_LOG("ctx %s exiting (be->open_ctxs %d)\n", ctx->name,
		   vvideo_kref_read(&be->open_ctxs));
}

/* vvideo_dev_t::lock must be Locked */

static VVIDEO_NOINLINE void vvideo_ctx_kref_busy_release(struct kref *kref_busy)
{
	struct vvideo_ctx_t *ctx =
		container_of(kref_busy, struct vvideo_ctx_t, kref_busy);
	/*
	 * kref_busy counts the number of threads concurrently operating on
	 * context and using DMABUF table. We could not have any.
	 */
	if (ctx->streamoff_pending) {
		VVIDEO_LOG_DMABUF("ctx %s streamoff_pending %#x\n", ctx->name,
				  ctx->streamoff_pending);
		do {
			int bit = __fls(ctx->streamoff_pending);

			vvideo_dmabuf_release_buf_type(ctx, bit);
#ifdef VVIDEO_DQBUF
			ctx->qbuf[bit] = 0;
#endif
			ctx->streamoff_pending &= ~(1 << bit);
		} while (ctx->streamoff_pending);
	}
}

/* vvideo_dev_t::lock must be Locked */

static void vvideo_ctx_send_release_msgs(struct vvideo_ctx_t *ctx)
{
	while (ctx->release_msgs) {
		struct vvideo_msg_t *m = ctx->release_msgs;

		ctx->release_msgs = m->next;
		VVIDEO_ASSERT(ctx->dev, m->ctx == ctx);
		VVIDEO_ASSERT(ctx->dev, m->dev == ctx->dev);
		m->ctx = NULL; /* Avoid stale pointer */
		m->dev = NULL; /* Avoid stale pointer */
		vmq_msg_return(m->vl->link, m->req);
		/* "m" does not belong to us anymore */
		++ctx->dev->stats.release_sent;
	}
}

/* vvideo_dev_t::lock must be Locked */

static VVIDEO_NOINLINE void vvideo_ctx_kref_used_release(struct kref *kref_used)
{
	struct vvideo_ctx_t *ctx =
		container_of(kref_used, struct vvideo_ctx_t, kref_used);
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	/*
	 * kref_used counts the number of threads concurrently operating on
	 * context. We could not have any, but the context will remain
	 * open (ctx->file != NULL). kref_used makes sure a close performed
	 * from one thread is deferred till other threads have finished
	 * their calls. We do not additionally increment and decrement
	 * kref_used on open and close.
	 */
	if (ctx->state == VVIDEO_CS_CLOSE_PENDING) {
		VVIDEO_LOG("ctx %s closing\n", ctx->name);
		ctx->state = VVIDEO_CS_BEING_CLOSED;
		/*
		 * Release device lock before possibly long-running
		 * context close call, as it locks other contexts
		 * as well.
		 */
		mutex_unlock(&vvideo_dev->lock);
		vvideo_ctx_close(ctx); /* void */
		mutex_lock(&vvideo_dev->lock);
		vvideo_ctx_send_release_msgs(ctx);
		vvideo_ctx_free(ctx);
	}
}

/*
 * Called when a thread has finished processing any
 * context-related request.
 * vvideo_dev_t::lock must be Unlocked
 */

static VVIDEO_NOINLINE void vvideo_ctx_release(struct vvideo_ctx_t *ctx,
						_Bool busy_too)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;

	VVIDEO_ASSERT(vvideo_dev, ctx->state != VVIDEO_CS_FREE &&
		      ctx->state != VVIDEO_CS_BEING_OPENED &&
		      ctx->state != VVIDEO_CS_BEING_CLOSED);
	VVIDEO_ASSERT(vvideo_dev, ctx->state == VVIDEO_CS_OPEN ||
		      ctx->state == VVIDEO_CS_CLOSE_PENDING);
	VVIDEO_ASSERT(vvideo_dev,
		!busy_too || vvideo_kref_read(&ctx->kref_busy) > 0);
	VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&ctx->kref_used) > 0);
	/*
	 * kref_put_mutex() atomically locks vvideo_dev->lock
	 * before calling function on last put.
	 */
	if (busy_too && kref_put_mutex(&ctx->kref_busy,
	    vvideo_ctx_kref_busy_release, &vvideo_dev->lock))
		mutex_unlock(&vvideo_dev->lock);
	if (kref_put_mutex(&ctx->kref_used, vvideo_ctx_kref_used_release,
			   &vvideo_dev->lock))
		mutex_unlock(&vvideo_dev->lock);
}

/* No lock used or expected */

static struct vvideo_dev_t *vvideo_link_find_dev(struct vvideo_link_t *vl,
					  unsigned int be_context)
{
	const unsigned int minor = be_context & vl->dev_mask;

	if (minor >= vl->configs.count)
		return NULL;
	return vl->devs + minor;
}

/*
 * Context returned must be released using vvideo_ctx_release()
 * when no more accessed by a thread.
 * vvideo_dev_t::lock must be Unlocked
 */

static VVIDEO_NOINLINE struct vvideo_ctx_t *vvideo_dev_find_be_context(
			struct vvideo_dev_t *vvideo_dev,
			    const unsigned int be_context)
{
	struct vvideo_ctx_t *ctx;
	int ctx_slot;

	/* 0 is invalid because it would match a free context slot */
	if (!be_context)
		return NULL;
	mutex_lock(&vvideo_dev->lock);
	VVIDEO_FOR_ALL_CTXS(vvideo_dev, ctx_slot, ctx)
	{
		if (ctx->be_context != be_context)
			continue;
		VVIDEO_ASSERT(vvideo_dev, ctx->state != VVIDEO_CS_FREE);
		VVIDEO_ASSERT(vvideo_dev, ctx->state != VVIDEO_CS_BEING_OPENED);
		VVIDEO_ASSERT(vvideo_dev,
			      ctx->state == VVIDEO_CS_OPEN ||
			      ctx->state == VVIDEO_CS_CLOSE_PENDING ||
			      ctx->state == VVIDEO_CS_BEING_CLOSED);
		if (ctx->state != VVIDEO_CS_OPEN)
			break;
		/* if kref_busy zero, context open, not used by any thread */
		vvideo_kref_get_unlocked(&ctx->kref_busy);
		/* if kref_used zero, context open, not used by any thread */
		vvideo_kref_get_unlocked(&ctx->kref_used);
		mutex_unlock(&vvideo_dev->lock);
		return ctx;
	}
	mutex_unlock(&vvideo_dev->lock);
	return NULL;
}

/*----- Code -----*/

static int vvideo_dev_check_node_name(struct vvideo_dev_t *vvideo_dev,
			   const int node,
			   const char *const name,
			   const _Bool message_on_all_errors)
{
#ifdef VVIDEO_USE_FD
	struct vvideo_be_t *be = vvideo_dev->link->be;
	int fd;
#else
	struct file *file;
#endif
	char buf[256];
	int ret;

	snprintf(buf, sizeof(buf), "/sys/class/video4linux/video%d/name", node);
#ifdef VVIDEO_USE_FD
	fd = be->sys_open(buf, O_RDONLY, 0);
	if (fd < 0) {
		if (message_on_all_errors)
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.could_not_open,
				"Could not open '%s' (%d)\n", buf, fd);
		return fd;
	}
	ret = be->sys_read(fd, buf, sizeof(buf));
	vvideo_sys_close(fd);
#else
	file = filp_open(buf, O_RDONLY, 0);
	if (IS_ERR(file)) {
		if (message_on_all_errors)
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.could_not_open,
					   "Could not open '%s' (%d)\n",
					   buf, (int) PTR_ERR(file));
		return (int) PTR_ERR(file);
	}
	ret = vvideo_kernel_read(file, buf, sizeof(buf), 0);
	filp_close(file, NULL);
#endif
	if (ret < 0) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.could_not_read,
				   "Could not read from node %d for '%s'\n",
				   node, name);
		return ret;
	}
	buf[ret] = '\0';
	while (ret > 0 && buf[ret - 1] == '\n')
		buf[--ret] = '\0';
	if (strcmp(name, buf)) {
		if (message_on_all_errors)
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.name_mismatch,
				"Node %d name '%s' mismatch, got '%s'\n",
				   node, name, buf);
		return -ENOENT;
	}
	VVIDEO_LOG("Found node %d name %s\n", node, name);
	return node;
}

/* No lock is used or expected */

static inline const struct vvideo_config_t *vvideo_dev_config(
					const struct vvideo_dev_t *vvideo_dev)
{
	return vvideo_dev->link->configs.configs + vvideo_dev->minor;
}

static int vvideo_dev_find_node(struct vvideo_dev_t *vvideo_dev)
{
	const struct vvideo_config_t *const c = vvideo_dev_config(vvideo_dev);
	int node;

	/* Name is required so that frontend can create device immediately */
	if (!*c->name) {
		VVIDEO_ERR("Missing device name\n");
		return -EINVAL;
	}
	/* If both name and orig_node are specified, check for match */
	if (c->orig_node >= 0) {
		VVIDEO_LOG("Checking node %d name %s\n", c->orig_node, c->name);
		return vvideo_dev_check_node_name(vvideo_dev, c->orig_node,
						  c->name, true);
	}
	/* Only a name is specified, so scan all nodes */
	for (node = 0; node < 256; ++node) {
		int ret;

		ret = vvideo_dev_check_node_name(vvideo_dev, node, c->name,
						 false);
		if (ret >= 0)
			return ret;
	}
	VVIDEO_ERR("Did not find name '%s'\n", c->name);
	return -ENOENT;
}

/*
 * Only called from vvideo_thread()
 * m->dev and t->m must be Set
 * vvideo_dev_t::lock must be Unlocked
 * vvideo_be_t::lock must be Unlocked
 */

static VVIDEO_NOINLINE int vvideo_req_open(struct vvideo_thread_t *t)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_dev_t *vvideo_dev = m->dev;
	int flags = O_RDWR;
	char path[32];
	int ret;
#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
	struct vvideo_link_t *vl = vvideo_dev->link;
	struct pv_driver_header_v1_t *ver = NULL;
#endif

	VVIDEO_ASSERT(vvideo_dev, !m->ctx);
	VVIDEO_LOG_VERBOSE("device %s %s\n", vvideo_dev->name, t->name);
	m->ctx = vvideo_ctx_alloc(vvideo_dev);
	if (!m->ctx) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.too_many_contexts,
			"%s, too many contexts on %s\n",
			    t->name, vvideo_dev->name);
		return -ENOMEM;
	}
	ret = vvideo_dev_find_node(vvideo_dev);
	if (ret < 0) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.find_node,
			"ctx %s %s did not find device '%s'\n",
			m->ctx->name, t->name,
			    vvideo_dev_config(vvideo_dev)->name);
		goto out;
	}
	snprintf(path, sizeof(path), "/dev/video%d", ret);
	if (m->req->u.open.flags & O_NONBLOCK)
		flags |= O_NONBLOCK;
#ifdef VVIDEO_USE_FD
	ret = t->be->sys_open(path, flags, 0);
	if (ret < 0) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.sys_open,
			"device %s failed to open '%s' for '%s' (%d)\n",
			   vvideo_dev->name, path,
			   vvideo_dev_config(vvideo_dev)->name, ret);
		goto out;
	}
	VVIDEO_LOG_VERBOSE("device %s path '%s' got fd %d\n",
		vvideo_dev->name, path, ret);
	m->ctx->fd = ret;
#else
	m->ctx->file = filp_open(path, flags, 0);
	if (IS_ERR(m->ctx->file)) {
		ret = (int)PTR_ERR(m->ctx->file);
		m->ctx->file = NULL;
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.filp_open,
			"device %s failed to open '%s' for '%s' (%d)\n",
			   vvideo_dev->name, path,
			   vvideo_dev_config(vvideo_dev)->name, ret);
		goto out;
	}
	VVIDEO_LOG_VERBOSE("device %s path '%s' got file %p\n",
		vvideo_dev->name, path, m->ctx->file);
#endif
	m->req->be_context = m->ctx->be_context;
	mutex_lock(&vvideo_dev->lock);
	m->ctx->state = VVIDEO_CS_OPEN;
	mutex_unlock(&vvideo_dev->lock);
	vvideo_kref_get_mutex(&vvideo_dev->link->be->open_ctxs, &t->be->lock);
#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
	if (vl->fe_version == 0 && vl->be_version == 0) {
		/*
		 * Set vvideo-be pv version
		 */
		ver = &m->req->vvideo_ver;
		set_be_driver_version(ver, VVIDEO_BE_VERSION);
		vl->fe_version = get_fe_driver_version(ver);
		vl->fe_version |= PV_VERSION_VALID_CODE;
		vl->be_version = ver->be_version;
		VVIDEO_INFO("fe_version(%#x), be_version(%#x)\n",
			vl->fe_version, vl->be_version);
	}
#endif
	VVIDEO_LOG("device %s %s opened %s ctx %s\n", vvideo_dev->name,
			   t->name, vvideo_dev_config(vvideo_dev)->name,
			   m->ctx->name);
	/* vvideo_ctx_release() done on return */
	/* Do not return an fd to frontend, it expects 0 */
	return 0;

out:
	mutex_lock(&vvideo_dev->lock);
	vvideo_ctx_free(m->ctx);
	m->ctx = NULL;
	mutex_unlock(&vvideo_dev->lock);
	return ret;
}

static void vvideo_ctx_cancel_polls(struct vvideo_ctx_t *ctx);

/*
 * Called on VVIDEO2_REQ_RELEASE.
 * vvideo_dev_t::lock must be Unlocked
 */

static VVIDEO_NOINLINE int vvideo_req_release(struct vvideo_thread_t *t)
{
	struct vvideo_ctx_t *ctx = t->m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;

	VVIDEO_LOG("%s, ctx %s\n", t->name, ctx->name);
	mutex_lock(&vvideo_dev->lock);
	switch (ctx->state) {
	case VVIDEO_CS_FREE:
	case VVIDEO_CS_BEING_OPENED: /* Impossible */
	case VVIDEO_CS_BEING_CLOSED:
		mutex_unlock(&vvideo_dev->lock);
		++vvideo_dev->errors.release_invalid;
		return -EINVAL;

	case VVIDEO_CS_OPEN:
	case VVIDEO_CS_CLOSE_PENDING:
		break;
	}
	/* Actual close will be performed by vvideo_ctx_release() */
	ctx->state = VVIDEO_CS_CLOSE_PENDING;
	vvideo_ctx_cancel_polls(ctx);
	mutex_unlock(&vvideo_dev->lock);
	VVIDEO_LOG("%s, ctx %s DONE\n", t->name, ctx->name);
	return 0;
}

static void vvideo_be_ctx_put(struct vvideo_be_t *be);

/*
 * Called on link loss and from vvideo_dev_cleanup().
 * Must be called from a vvideo_thread,
 * because it operates on file descriptors.
 * vvideo_dev_t::lock can be Locked
 */

static VVIDEO_NOINLINE void vvideo_dev_release_ctxs(
						struct vvideo_dev_t *vvideo_dev)
{
	struct vvideo_ctx_t *ctx;
	int ctx_slot;

	VVIDEO_LOG("device %s entered\n", vvideo_dev->name);
	VVIDEO_FOR_ALL_CTXS(vvideo_dev, ctx_slot, ctx)
	{
		VVIDEO_ASSERT(vvideo_dev, ctx->state == VVIDEO_CS_FREE ||
			      ctx->state == VVIDEO_CS_OPEN ||
			      ctx->state == VVIDEO_CS_CLOSE_PENDING);
		VVIDEO_ASSERT(vvideo_dev,
			      ctx->state != VVIDEO_CS_BEING_OPENED &&
			      ctx->state != VVIDEO_CS_BEING_CLOSED);
		if (ctx->state == VVIDEO_CS_FREE)
			continue;
		/* Satisfy assert in vvideo_ctx_release() */
		ctx->state = VVIDEO_CS_CLOSE_PENDING;
		/*
		 * This schedules deferred kref_put(ctx), which must
		 * be processed before vvideo_ctx_free() is called.
		 */
		vvideo_ctx_cancel_polls(ctx);
		/* Perform deferred kref_put(ctx). Calls vvideo_ctx_release() */
		vvideo_be_ctx_put(vvideo_dev->link->be);
		/*
		 * vvideo_ctx_release() may have closed and freed ctx
		 * through vvideo_ctx_kref_release().
		 */
		if (ctx->state == VVIDEO_CS_FREE)
			continue;
		/*
		 * Satisfy assert in vvideo_ctx_close(), as we can still
		 * be in VVIDEO_CS_CLOSE_PENDING.
		 */
		ctx->state = VVIDEO_CS_BEING_CLOSED;
		vvideo_ctx_close(ctx);
		vvideo_ctx_send_release_msgs(ctx);
		vvideo_ctx_free(ctx);
	}
	VVIDEO_LOG("device %s exited\n", vvideo_dev->name);
}

static int vvideo_thread_querycap_after(struct vvideo_thread_t *t, int ret)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_capability *c = (void *) req_ioctl->arg;

	if (ret)
		return ret;
#ifdef VVIDEO_DEBUG
	VVIDEO_LOG("ctx %s %s returned:\n",
		   ctx->name, vvideo_ioctl_name(t->ioctl.cmd));
	vvideo_v4l2_capability_cookie(vvideo_printk, 0, c);
#endif
	if (c->capabilities & V4L2_CAP_FENCES) {
		++ctx->dev->stats.fences_ok;
#if !IS_ENABLED(CONFIG_VLX_VFENCE2)
		c->capabilities &= ~V4L2_CAP_FENCES;
		++ctx->dev->stats.fences_removed;
#endif
	}
#ifdef V4L2_CAP_DEVICE_CAPS
	if (c->device_caps & V4L2_CAP_FENCES) {
		++ctx->dev->stats.fences_ok;
#if !IS_ENABLED(CONFIG_VLX_VFENCE2)
		c->device_caps &= ~V4L2_CAP_FENCES;
		++ctx->dev->stats.fences_removed;
#endif
	}
#endif
	return ret;
}

static const struct vvideo_call_t vvideo_call_querycap = {
	.cmd = VIDIOC_QUERYCAP,
	.after = vvideo_thread_querycap_after
};

static int vvideo_thread_buffer_before(struct vvideo_thread_t *t)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_buffer *b = &m->secure.b;
	size_t planes_size;
	int plane;
	struct vvideo_dmabuf_gid_t *gids;
	int ret;
	unsigned int flags;

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	t->ioctl.fence_fd = -1;
#endif
	/*
	 * Pass V4L2 a validated secure local copy, because
	 * even if V4L2 makes its own copy, it is not able
	 * to properly handle invocations from a non-user
	 * thread for b->memory == V4L2_MEMORY_USERPTR
	 * and will crash.
	 */
	memcpy(b, req_ioctl->arg, sizeof(*b));
	t->ioctl.arg = (unsigned long) b;

	/* For VIDIOC_QUERYBUF, b->memory is not set on entry */
	if (b->memory != V4L2_MEMORY_DMABUF &&
	    t->ioctl.cmd != VIDIOC_QUERYBUF) {
		VVIDEO_ERR_LIMITED(ctx->dev->errors.not_dmabuf,
			"ctx %s %s (%d) not supported, cmd %#x (%s)\n",
			ctx->name, vvideo_v4l2_memory(b->memory),
			b->memory, t->ioctl.cmd,
			vvideo_ioctl_name(t->ioctl.cmd));
		return -EINVAL;
	}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	/* No v4l2_buffer::fence_fd in Exynos Linux 5.4 */
	if (b->reserved != VFENCE2_ID_NONE &&
	    b->flags & V4L2_BUF_FLAG_IN_FENCE) {
		ret = vfence2_import_fence(ctx->dev->link->vfence2_connection,
			b->reserved, &t->ioctl.fence_fd);
		if (ret) {
			VVIDEO_ERR_LIMITED(ctx->dev->errors.import_fence,
		    "ctx %s cmd %#x (%s) vfence2_import_fence failed (%d)\n",
				ctx->name, t->ioctl.cmd,
				vvideo_ioctl_name(t->ioctl.cmd), ret);
			return ret;
		}
		b->reserved = t->ioctl.fence_fd;
	}
#endif
	if (!V4L2_TYPE_IS_MULTIPLANAR(b->type))
		return 0;
	b->m.planes = (struct v4l2_plane *)req_ioctl->ext;
	planes_size = sizeof(struct v4l2_plane) * b->length;
	if (t->ioctl.cmd != VIDIOC_QUERYBUF && b->length >
	    VVIDEO2_REQ_IOCTL_EXT_SIZE / sizeof(struct v4l2_plane)) {
		VVIDEO_ERR_LIMITED(ctx->dev->errors.b_length,
				   "ctx %s b->length %u too large\n", ctx->name,
				   b->length);
		ret = -EINVAL;
		goto error;
	}
	switch (t->ioctl.cmd) {
	case VIDIOC_QBUF:
		flags = VVIDEO_DMABUF_QUEUED | VVIDEO_DMABUF_RUNNING;
		break;
	case VIDIOC_PREPARE_BUF:
		flags = VVIDEO_DMABUF_PREPARED | VVIDEO_DMABUF_RUNNING;
		break;
	default:
		return 0;
	}
	gids = (void *)(req_ioctl->ext + planes_size);
#ifdef VVIDEO_DMABUF
	for (plane = 0; plane < b->length; ++plane) {
		char buf[VVIDEO_GID_STRING];

		VVIDEO_LOG_DMABUF("ctx %s before %s: gids [%d] = %s\n",
			ctx->name, vvideo_ioctl_name(t->ioctl.cmd),
			plane, vvideo_dmabuf_gid_format(gids + plane,
			buf, sizeof(buf)));
	}
#endif
	for (plane = 0; plane < b->length; ++plane) {
		ret = vvideo_dmabuf_gid2fd(ctx, gids + plane, b->index, plane,
					   b->type, flags);
		if (VVIDEO_FD_VALID(ret)) {
			b->m.planes[plane].m.fd = ret;
			continue;
		}
		/* Error message already issued */
		/* Error accounting already done */
		while (plane-- > 0)
			vvideo_dmabuf_release_fd(ctx, b->m.planes[plane].m.fd,
				flags);
		goto error;
	}
#ifdef VVIDEO_VERBOSE
	VVIDEO_LOG_VERBOSE("ctx %s Buffer after gid2fd for %s:\n",
		ctx->name, vvideo_ioctl_name(t->ioctl.cmd));
	vvideo_v4l2_buffer_cookie(vvideo_printk, 0, b);
#endif
	return 0;

error:
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (VVIDEO_FD_VALID(t->ioctl.fence_fd))
		vvideo_ctx_close_fd(ctx, t->ioctl.fence_fd);
#endif
	return ret;
}

static int vvideo_thread_buffer_after(struct vvideo_thread_t *t, int ret)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_buffer *b = &m->secure.b;
	size_t planes_size;
	struct vvideo_dmabuf_gid_t *gids;
	int plane;
	unsigned int flags;

	if (ret) {
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		if (VVIDEO_FD_VALID(t->ioctl.fence_fd))
			vvideo_ctx_close_fd(ctx, t->ioctl.fence_fd);
#endif
		if (t->ioctl.cmd != VIDIOC_QBUF &&
		    t->ioctl.cmd != VIDIOC_PREPARE_BUF)
			return ret;
	}
#ifdef VVIDEO_VERBOSE
	VVIDEO_LOG_VERBOSE("ctx %s Buffer before fd2gid:\n", ctx->name);
	vvideo_v4l2_buffer_cookie(vvideo_printk, 0, b);
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (!ret && b->reserved >= 0 && b->flags & V4L2_BUF_FLAG_OUT_FENCE) {
		vfence2_id_t vfid;

		ret = vfence2_export_fence(ctx->dev->link->vfence2_connection,
				b->reserved, &vfid, 0 /*can_block*/);
		if (ret) {
			VVIDEO_WARN_LIMITED(ctx->dev->warnings.export_fence,
		    "ctx %s cmd %#x (%s) vfence2_export_fence failed (%d)\n",
				ctx->name, t->ioctl.cmd,
				vvideo_ioctl_name(t->ioctl.cmd), ret);
			ret = 0;
			vfid = VFENCE2_ID_NONE;
		}
		/*
		 * Close local fence reference, vdriver
		 * took it over on success.
		 */
		vvideo_ctx_close_fd(ctx, b->reserved);
		if (vfid == VFENCE2_ID_NONE) {
			b->flags &= ~V4L2_BUF_FLAG_OUT_FENCE;
			vfid = -1; /*Make sure frontend returns -1*/
		}
		b->reserved = vfid;
	}
#endif
	if (!V4L2_TYPE_IS_MULTIPLANAR(b->type))
		goto update_pmem;
	VVIDEO_ASSERT(ctx->dev, b->m.planes ==
		      (struct v4l2_plane *)req_ioctl->ext);
	if (b->m.planes != (struct v4l2_plane *)req_ioctl->ext) {
		VVIDEO_WARN(
		"ctx %s %s, fixing b->m.planes %p != req_ioctl->ext %p\n",
			ctx->name, t->name, b->m.planes, req_ioctl->ext);
		b->m.planes = (struct v4l2_plane *)req_ioctl->ext;
	}
	switch (t->ioctl.cmd) {
	case VIDIOC_QUERYBUF:
	default:
		/* QUERYBUF does not return FDs */
		goto update_pmem;
	case VIDIOC_QBUF:
		flags = VVIDEO_DMABUF_RUNNING;
		if (ret)
			flags |= VVIDEO_DMABUF_QUEUED;
		break;
	case VIDIOC_PREPARE_BUF:
		flags = VVIDEO_DMABUF_RUNNING;
		if (ret)
			flags |= VVIDEO_DMABUF_PREPARED;
		break;
	case VIDIOC_DQBUF:
		flags = VVIDEO_DMABUF_QUEUED | VVIDEO_DMABUF_DELETED;
		/* We never come here with error "ret" */
		if (b->flags & V4L2_BUF_FLAG_ERROR)
			++ctx->dev->stats.flag_error;
		break;
	}
	planes_size = sizeof(struct v4l2_plane) * b->length;
	gids = (struct vvideo_dmabuf_gid_t *) (req_ioctl->ext + planes_size);
	for (plane = 0; plane < b->length; ++plane)
		if (vvideo_dmabuf_fd2gid(ctx, b->m.planes[plane].m.fd,
					gids + plane, flags, t->ioctl.cmd) < 0)
			VVIDEO_ZERO(gids + plane);
#ifdef VVIDEO_DMABUF
	for (plane = 0; plane < b->length; ++plane) {
		char buf[VVIDEO_GID_STRING];

		VVIDEO_LOG_DMABUF("ctx %s after %s: gids [%d] = %s\n",
			ctx->name, vvideo_ioctl_name(t->ioctl.cmd), plane,
			vvideo_dmabuf_gid_format(gids + plane, buf,
			sizeof(buf)));
	}
#endif
update_pmem:
	/* Update PMEM from secure local copy */
	memcpy(req_ioctl->arg, b, sizeof(*b));
	return ret;
}

static const struct vvideo_call_t vvideo_call_querybuf = {
	.cmd = VIDIOC_QUERYBUF,
	.before = vvideo_thread_buffer_before,
	.after = vvideo_thread_buffer_after
};

static int vvideo_thread_dqbuf_before(struct vvideo_thread_t *t)
{
	if (t->deferred_pending) {
		t->deferred_pending = 0;
		vmq_msg_return_flush(t->m->ctx->dev->link->link);
	}
	return vvideo_thread_buffer_before(t);
}

#ifdef VVIDEO_DPBS
static int vvideo_thread_dpbs_export(struct vvideo_thread_t *t, int *fd)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_dmabuf_gid_t gid;
	int ret;

	/* 0 is reserved and cannot be a valid DMABUF file descriptor */
	if (!*fd) {
		++vvideo_dev->stats.dpbs.zero;
		return -EINVAL;
	}
	ret = vvideo_dmabuf_fd2gid(ctx, *fd, &gid, VVIDEO_DMABUF_DELETED,
				t->ioctl.cmd);
	if (ret)
		return ret;
	++vvideo_dev->stats.dpbs.export;
	VVIDEO_LOG("ctx %s fd %d gid %#x\n", ctx->name, *fd, gid.gid);
	*fd = gid.gid;
	return 0;
}

static void vvideo_thread_dpbs_dqbuf_after(struct vvideo_thread_t *t)
{
	struct v4l2_buffer *b = &t->m->secure.b;
	struct vvideo_ctx_t *ctx = t->m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct mfc_dec_dpb_ref_info *info;
	int buffer;

	if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return;
	if (!ctx->dpbs.info)
		return;
	if (b->index >= MFC_MAX_BUFFERS) {
		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.dpbs.index,
			"ctx %s b->index %d too high\n", ctx->name, b->index);
		return;
	}
	/* Shared memory could be too small */
	if (b->index >= ctx->dpbs.dma_buf->size / sizeof(*info)) {
		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.dpbs.size,
			"ctx %s b->index %d too high for size %#lx\n",
			ctx->name, b->index, ctx->dpbs.dma_buf->size);
		return;
	}
	info = ctx->dpbs.info + b->index;
	VVIDEO_LOG("ctx %s index %d/%d\n", ctx->name, b->index, info->index);
	++vvideo_dev->stats.dpbs.dqbuf;
	for (buffer = 0; buffer < MFC_MAX_BUFFERS; ++buffer) {
		struct mfc_stored_dpb_info *dpb = &info->dpb[buffer];
		int plane;

		if (dpb->fd[0] < 0)
			break; /* End of buffers */
		++vvideo_dev->stats.dpbs.buffer;
		if (dpb->fd[0] > 0)
			VVIDEO_LOG("slot %d: %d %d %d\n",
				buffer, dpb->fd[0], dpb->fd[1], dpb->fd[2]);
		for (plane = 0; plane < MFC_MAX_PLANES; ++plane) {
			if (dpb->fd[plane] < 0)
				break; /* End of planes */
			++vvideo_dev->stats.dpbs.plane;
			if (vvideo_thread_dpbs_export(t, dpb->fd + plane)) {
				dpb->fd[plane] = VDMAHEAP_GID_INVALID;
				/*
				 * Given that -1 is both the sole invalid FD
				 * and an end-of-list indicator, it is not
				 * possible to have invalid FDs followed by
				 * valid ones, so break on error.
				 */
				break;
			}
		}
	}
}
#endif

static int vvideo_thread_dqbuf_after(struct vvideo_thread_t *t, int ret)
{
#ifdef VVIDEO_DQBUF
	struct v4l2_buffer *b = &t->m->secure.b;
	struct vvideo_ctx_t *ctx = t->m->ctx;

	if (!ret && b->index < sizeof(ctx->qbuf[0]) * 8) {
		mutex_lock(&ctx->dev->lock);
		if (!(ctx->qbuf[b->type] & (1 << b->index)))
			VVIDEO_WARN_LIMITED(ctx->dev->warnings.dup_dqbuf,
				"ctx %s type %d (%s) dup dqbuf %d\n", ctx->name,
				b->type, vvideo_v4l2_buf_type_short(b->type),
				b->index);
		ctx->qbuf[b->type] &= ~(1 << b->index);
		mutex_unlock(&ctx->dev->lock);
	}
#endif
#ifdef VVIDEO_DPBS
	/*
	 * Process shared memory and file descriptors inside before
	 * the file descriptors are closed during execution of
	 * vvideo_thread_buffer_after().
	 * Note that vvideo_thread_buffer_after() does not generate
	 * new errors, so there is nothing to undo.
	 */
	if (!ret)
		vvideo_thread_dpbs_dqbuf_after(t);
#endif
	return vvideo_thread_buffer_after(t, ret);
}

static const struct vvideo_call_t vvideo_call_dqbuf = {
	.cmd = VIDIOC_DQBUF,
	.before = vvideo_thread_dqbuf_before,
	.after = vvideo_thread_dqbuf_after
};

static int vvideo_thread_qbuf_before(struct vvideo_thread_t *t)
{
	const int ret = vvideo_thread_buffer_before(t);

#ifdef VVIDEO_DQBUF
	if (!ret) {
		struct v4l2_buffer *b = &t->m->secure.b;

		if (b->index < sizeof(t->m->ctx->qbuf[0]) * 8) {
			struct vvideo_ctx_t *ctx = t->m->ctx;

			mutex_lock(&ctx->dev->lock);
			if (ctx->qbuf[b->type] & (1 << b->index))
				VVIDEO_WARN_LIMITED(
					ctx->dev->warnings.dup_qbuf,
					"ctx %s type %d (%s) dup qbuf %d\n",
					ctx->name, b->type,
					vvideo_v4l2_buf_type_short(b->type),
					b->index);
			ctx->qbuf[b->type] |= 1 << b->index;
			mutex_unlock(&ctx->dev->lock);
		}
	}
#endif
	return ret;
}

static int vvideo_thread_qbuf_after(struct vvideo_thread_t *t, int ret)
{
#ifdef VVIDEO_DQBUF
	struct v4l2_buffer *b = &t->m->secure.b;

	if (ret && b->index < sizeof(t->m->ctx->qbuf[0]) * 8) {
		struct vvideo_ctx_t *ctx = t->m->ctx;

		/* VIDIOC_QBUF failed, so buffer index unused */
		mutex_lock(&ctx->dev->lock);
		ctx->qbuf[b->type] &= ~(1 << b->index);
		mutex_unlock(&ctx->dev->lock);
	}
#endif
	return vvideo_thread_buffer_after(t, ret);
}

static const struct vvideo_call_t vvideo_call_qbuf = {
	.cmd = VIDIOC_QBUF,
	.before = vvideo_thread_qbuf_before,
	.after = vvideo_thread_qbuf_after
};

static const struct vvideo_call_t vvideo_call_prepare_buf = {
	.cmd = VIDIOC_PREPARE_BUF,
	.before = vvideo_thread_buffer_before,
	.after = vvideo_thread_buffer_after
};

static int vvideo_msg_expbuf_after(struct vvideo_thread_t *t, int ret)
{
	struct vvideo_msg_t *m = t->m;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_v4l2_exportbuffer *eb = (void *)req_ioctl->arg;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	char buf[VVIDEO_GID_STRING];
	struct dma_buf *dma_buf;

	if (ret)
		return ret;
	dma_buf = dma_buf_get(eb->eb.fd); /* Increases f_count */
	if (IS_ERR(dma_buf)) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dma_buf_get,
			"ctx %s dma_buf_get(%d) failed (%d)\n", ctx->name,
			eb->eb.fd, (int)PTR_ERR(dma_buf));
		return PTR_ERR(dma_buf);
	}
	/* Increase dma_buf reference count */
	ret = vvideo_dmabuf_export(ctx, dma_buf, &eb->gid);
	dma_buf_put(dma_buf); /* void */
	if (ret) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dmabuf_export,
			"ctx %s vvideo_dmabuf_export(%d) failed (%d)\n",
			ctx->name, eb->eb.fd, ret);
		return ret;
	}
	(void) buf;
	VVIDEO_LOG("ctx %s fd %d gid %s\n", ctx->name, eb->eb.fd,
		vvideo_dmabuf_gid_format(&eb->gid, buf, sizeof(buf)));
	return 0;
}

static const struct vvideo_call_t vvideo_call_expbuf = {
	.cmd = VIDIOC_EXPBUF,
	.after = vvideo_msg_expbuf_after
};

#ifdef VVIDEO_DEBUG
static int vvideo_thread_stream_before(struct vvideo_thread_t *t)
{
	struct VVideo2Ioctl *req_ioctl = &t->m->req->u.ioctl;
	const enum v4l2_buf_type buf_type = *(const int *)req_ioctl->arg;

	VVIDEO_LOG("ctx %s type: %d (%s)\n", t->m->ctx->name,
		   buf_type, vvideo_v4l2_buf_type_short(buf_type));
	return 0;
}
#endif

static int vvideo_thread_streamoff_after(struct vvideo_thread_t *t, int ret)
{
	struct vvideo_ctx_t *ctx = t->m->ctx;
	struct VVideo2Ioctl *req_ioctl = &t->m->req->u.ioctl;
	const enum v4l2_buf_type buf_type = *(const int *)req_ioctl->arg;

	if (ret)
		return ret;
	/*
	 * There can be still threads in context about to exit from DQBUF,
	 * which will need to access the DMABUF table in order to
	 * map the local fd back to gid/cred, so deferring cleaning
	 * the table entries until all threads have exited context.
	 */
	mutex_lock(&ctx->dev->lock);
	ctx->streamoff_pending = 1 << buf_type;
	VVIDEO_LOG_DMABUF("ctx %s streamoff_pending %#x\n", ctx->name,
			  ctx->streamoff_pending);
	mutex_unlock(&ctx->dev->lock);
	if (buf_type < VVIDEO_BUF_TYPE_MAX)
		ctx->streamon[buf_type] = 0;
	return ret;
}

static const struct vvideo_call_t vvideo_call_streamoff = {
	.cmd = VIDIOC_STREAMOFF,
#ifdef VVIDEO_DEBUG
	.before = vvideo_thread_stream_before,
#endif
	.after = vvideo_thread_streamoff_after
};

static int vvideo_thread_streamon_after(struct vvideo_thread_t *t, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &t->m->req->u.ioctl;
	const enum v4l2_buf_type buf_type = *(const int *)req_ioctl->arg;

	if (ret)
		return ret;
	/* streamon[] is for information only, so no lock */
	if (buf_type < VVIDEO_BUF_TYPE_MAX)
		t->m->ctx->streamon[buf_type] = 1;
	return ret;
}

static const struct vvideo_call_t vvideo_call_streamon = {
	.cmd = VIDIOC_STREAMON,
#ifdef VVIDEO_DEBUG
	.before = vvideo_thread_stream_before,
#endif
	.after = vvideo_thread_streamon_after
};

#ifdef VVIDEO_DPBS
static int vvideo_thread_dpbs_s_ctrl_before(struct vvideo_thread_t *t)
{
	struct VVideo2Ioctl *req_ioctl = &t->m->req->u.ioctl;
	struct v4l2_control *c = (void *) req_ioctl->arg;
	struct vvideo_ctx_t *ctx = t->m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	int ret = 0;

	if (c->id != V4L2_CID_MPEG_MFC_SET_USER_SHARED_HANDLE)
		return 0;
	mutex_lock(&vvideo_dev->lock);
	if (ctx->dpbs.dma_buf) {
		/* Do not allow changing once set */
		if (c->value == ctx->dpbs.fd) {
			++vvideo_dev->stats.dpbs.repeat;
			goto exit;
		}
		ret = -EBUSY;
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dpbs.multiple,
			"ctx %s fd %d already have handle %d\n",
			ctx->name, c->value, ctx->dpbs.fd);
		goto exit;
	}
	ctx->dpbs.fd = c->value;
	ctx->dpbs.dma_buf = dma_buf_get(ctx->dpbs.fd); /* Increases f_count */
	if (IS_ERR(ctx->dpbs.dma_buf)) {
		ret = PTR_ERR(ctx->dpbs.dma_buf);
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dpbs.shared_get,
			"ctx %s dma_buf_get(%d) failed (%d)\n",
			ctx->name, ctx->dpbs.fd, ret);
		ctx->dpbs.dma_buf = NULL;
		vvideo_ctx_dpbs_release(ctx); /* For dpbs.fd only */
		goto exit;
	}
	/* We get 16 KB from omxutc */
	VVIDEO_LOG("ctx %s dma_buf.size %#lx\n",
		ctx->name, ctx->dpbs.dma_buf->size);
	ctx->dpbs.info = dma_buf_vmap(ctx->dpbs.dma_buf);
	if (!ctx->dpbs.info) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dpbs.vmap,
			"ctx %s dma_buf_vmap(%d) failed\n",
			ctx->name, ctx->dpbs.fd);
		vvideo_ctx_dpbs_release(ctx);
		ret = -ENOMEM;
		goto exit;
	}
	++vvideo_dev->stats.dpbs.s_ctrl;
exit:
	mutex_unlock(&vvideo_dev->lock);
	return ret;
}
#endif

static int vvideo_thread_s_ctrl_before(struct vvideo_thread_t *t)
{
	struct VVideo2Ioctl *req_ioctl = &t->m->req->u.ioctl;
	struct v4l2_control *c = (void *) req_ioctl->arg;
	struct vvideo_ctx_t *ctx = t->m->ctx;
	int fd;
	int ret = 0;

	if (!vvideo_cid_user_shared_handle(c->id))
		return 0;
	/*
	 * buf_type 0 is not valid, so QUEUED is not going to be
	 * removed by a STREAMOFF.
	 */
	fd = vvideo_dmabuf_gid2fd(ctx, VVIDEO_CTRL_TO_GID(c),
		-1 /*buf_index*/, 0 /*plane*/, 0 /*buf_type*/,
		VVIDEO_DMABUF_RUNNING | VVIDEO_DMABUF_QUEUED);
	if (!VVIDEO_FD_VALID(fd))
		/* Error message already issued */
		return fd;
	c->value = fd;
#ifdef VVIDEO_DPBS
	ret = vvideo_thread_dpbs_s_ctrl_before(t);
	if (ret) /* Undo gid2fd from above */
		vvideo_dmabuf_fd2gid(ctx, fd, VVIDEO_CTRL_TO_GID(c),
			VVIDEO_DMABUF_RUNNING | VVIDEO_DMABUF_QUEUED,
			t->ioctl.cmd);
#endif
	return ret;
}

static int vvideo_thread_ctrl_after(struct vvideo_thread_t *t, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &t->m->req->u.ioctl;
	struct v4l2_control *c = (void *) req_ioctl->arg;
	unsigned int flags;

	if (!vvideo_cid_user_shared_handle(c->id))
		return ret;
	/* Convert FD back to GID */
	if (t->ioctl.cmd == VIDIOC_S_CTRL) {
		flags = VVIDEO_DMABUF_RUNNING;
		if (ret)
			flags |= VVIDEO_DMABUF_QUEUED;
	} else {
		/*
		 * Do not convert unset c->value into a GID, because 0
		 * is a valid fd, generally a user_shared_handle in
		 * case of MFC, so we would obtain a valid GID too.
		 */
		if (ret)
			return ret;
		flags = 0;
	}
	if (vvideo_dmabuf_fd2gid(t->m->ctx, c->value, VVIDEO_CTRL_TO_GID(c),
				flags, t->ioctl.cmd) < 0)
		VVIDEO_ZERO(VVIDEO_CTRL_TO_GID(c));
	return ret;
}

static const struct vvideo_call_t vvideo_call_g_ctrl = {
	.cmd = VIDIOC_G_CTRL,
	.after = vvideo_thread_ctrl_after
};

static const struct vvideo_call_t vvideo_call_s_ctrl = {
	.cmd = VIDIOC_S_CTRL,
	.before = vvideo_thread_s_ctrl_before,
	.after = vvideo_thread_ctrl_after
};

#define VVIDEO_EDID_SIZE	128

#define VVIDEO2_REQ_IOCTL_TOTAL_SIZE                                           \
	(VVIDEO2_REQ_IOCTL_ARG_SIZE + VVIDEO2_REQ_IOCTL_EXT_SIZE)

static int vvideo_msg_edid_before(struct vvideo_thread_t *t)
{
	struct vvideo_msg_t *m = t->m;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;
	struct v4l2_edid *edid = (void *)req_ioctl->arg;
	u8 *const req_blocks = (void *) (edid + 1);

	/* Take a secure copy of struct v4l2_edid */
	memcpy(&m->secure.edid, req_ioctl->arg, sizeof(m->secure.edid));
	edid = &m->secure.edid;
	t->ioctl.arg = (unsigned long) edid;
	/* Make sure pointer from frontend is within PMEM */
	edid->edid = req_blocks;
	/* Make sure blocks do not go beyond PMEM slot */
	if (edid->blocks > 256 || edid->blocks > (VVIDEO2_REQ_IOCTL_TOTAL_SIZE -
	    sizeof(struct v4l2_edid)) / VVIDEO_EDID_SIZE) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.edid_blocks,
			"ctx %s too many EDID blocks %u\n",
			m->ctx->name, edid->blocks);
		return -E2BIG;
	}
	return 0;
}

static int vvideo_msg_edid_after(struct vvideo_thread_t *t, int ret)
{
	struct vvideo_msg_t *m = t->m;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;

	if (ret)
		return ret;
	/* Update PMEM from secure local copy */
	memcpy(req_ioctl->arg, &m->secure.edid, sizeof(m->secure.edid));
	return ret;
}

static const struct vvideo_call_t vvideo_call_g_edid = {
	.cmd = VIDIOC_G_EDID,
	.before = vvideo_msg_edid_before,
	.after = vvideo_msg_edid_after
};

static const struct vvideo_call_t vvideo_call_s_edid = {
	.cmd = VIDIOC_S_EDID,
	.before = vvideo_msg_edid_before,
	.after = vvideo_msg_edid_after
};

#define VVIDEO2_EXT_CTRLS_MAX \
	(VVIDEO2_REQ_IOCTL_EXT_SIZE / sizeof(struct v4l2_ext_control))

static int vvideo_thread_ext_ctrls_before(struct vvideo_thread_t *t)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_ext_controls *p = (void *) req_ioctl->arg;
	const unsigned int p_count = p->count; /* Take snapshot */
	struct v4l2_ext_control *controls; /* req_ioctl->ext */
	struct vvideo_dmabuf_gid_t *gids;
	unsigned int flags;
	unsigned int i;
	unsigned int max_gid;
	unsigned int gid;
	int ret;

	VVIDEO_LOG("ctx %s %s count %d\n", ctx->name,
		vvideo_ioctl_name(t->ioctl.cmd), p_count);
	p->controls = (struct v4l2_ext_control *)req_ioctl->ext;
	if (!p_count)
		return 0;
	if (p_count > VVIDEO2_EXT_CTRLS_MAX) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.p_count,
			"ctx %s %s count %d too large\n", m->ctx->name,
			vvideo_ioctl_name(t->ioctl.cmd), p_count);
		return -EINVAL;
	}
	switch (t->ioctl.cmd) {
	case VIDIOC_G_EXT_CTRLS:
	default:
		return 0;
	case VIDIOC_S_EXT_CTRLS:
		flags = VVIDEO_DMABUF_RUNNING | VVIDEO_DMABUF_QUEUED;
		break;
	case VIDIOC_TRY_EXT_CTRLS:
		flags = VVIDEO_DMABUF_RUNNING;
		break;
	}
	controls = (void *) req_ioctl->ext;
	gids = (void *)(controls + p_count);
	max_gid = (sizeof(req_ioctl->ext) - p_count * sizeof(*controls)) /
		sizeof(*gids);
	for (i = 0, gid = 0; i < p_count; ++i) {
		struct v4l2_ext_control *c = controls + i;
		char buf[VVIDEO_GID_STRING];

		if (!vvideo_cid_user_shared_handle(c->id))
			continue;
		if (gid >= max_gid) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.ext_gid,
				"ctx %s cid %#x (%s) too many gids (%d))\n",
				m->ctx->name, c->id, vvideo_v4l2_cid(c->id),
				gid);
			ret = -E2BIG;
			goto exit_undo;
		}
		ret = vvideo_dmabuf_gid2fd(ctx, gids + gid, -1 /*buf_index*/,
			0 /*plane*/, 0 /*buf_type*/, flags);
		if (!VVIDEO_FD_VALID(ret))
			/* Error message already issued */
			goto exit_undo;
		c->value = ret;
		(void) buf;
		VVIDEO_LOG("ctx %s %s cid #%x (%s) gid %s fd %d\n",
			ctx->name, vvideo_ioctl_name(t->ioctl.cmd),
			c->id, vvideo_v4l2_cid(c->id),
			vvideo_dmabuf_gid_format(gids + gid, buf,
			sizeof(buf)), c->value);
		++gid;
	}
	return 0;

exit_undo:
	if (gid <= 0)
		return ret;
	while (i-- > 0) {
		struct v4l2_ext_control *c = controls + i;

		if (!vvideo_cid_user_shared_handle(c->id))
			continue;
		--gid;
		vvideo_dmabuf_release_fd(ctx, c->value, flags);
		if (gid <= 0)
			break;
	}
	return ret;
}

static int vvideo_thread_ext_ctrls_after(struct vvideo_thread_t *t, int ret)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_ext_controls *p = (void *)req_ioctl->arg;
	const unsigned int p_count = p->count; /* Take snapshot */
	struct v4l2_ext_control *controls;
	struct vvideo_dmabuf_gid_t *gids;
	unsigned int flags;
	unsigned int i;
	unsigned int max_gid;
	unsigned int gid;

	VVIDEO_LOG("ctx %s %s count %d ret %d\n", ctx->name,
		vvideo_ioctl_name(t->ioctl.cmd), p_count, ret);
	if (!p_count)
		return ret;
	if (p_count > VVIDEO2_EXT_CTRLS_MAX) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.p_count,
			"ctx %s %s count %d too large\n", ctx->name,
			vvideo_ioctl_name(t->ioctl.cmd), p_count);
		if (ret)
			return ret;
		return -EINVAL;
	}
	/* Convert FDs back to GIDs */
	switch (t->ioctl.cmd) {
	case VIDIOC_G_EXT_CTRLS:
	default:
		if (ret)
			return ret;
		flags = 0;
		break;
	case VIDIOC_S_EXT_CTRLS:
		flags = VVIDEO_DMABUF_RUNNING;
		if (ret)
			flags |= VVIDEO_DMABUF_QUEUED;
		break;
	case VIDIOC_TRY_EXT_CTRLS:
		flags = VVIDEO_DMABUF_RUNNING;
		break;
	}
	controls = (void *)req_ioctl->ext;
	gids = (void *)(controls + p_count);
	max_gid = (sizeof(req_ioctl->ext) - p_count * sizeof(*controls)) /
		sizeof(*gids);
	for (i = 0, gid = 0; i < p_count; ++i) {
		struct v4l2_ext_control *c = controls + i;
		char buf[VVIDEO_GID_STRING];

		if (!vvideo_cid_user_shared_handle(c->id))
			continue;
		if (gid >= max_gid) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.ext_gid,
				"ctx %s cid %#x (%s) too many gids (%d))\n",
				m->ctx->name, c->id,
				vvideo_v4l2_cid(c->id), gid);
			return -EINVAL;
		}
		if (vvideo_dmabuf_fd2gid(ctx, c->value, gids + gid, flags,
					t->ioctl.cmd) < 0)
			VVIDEO_ZERO(gids + gid);
		(void) buf;
		VVIDEO_LOG("ctx %s %s cid %#x (%s) fd %d gid %s\n",
			ctx->name, vvideo_ioctl_name(t->ioctl.cmd),
			c->id, vvideo_v4l2_cid(c->id), c->value,
			vvideo_dmabuf_gid_format(gids + gid, buf,
			sizeof(buf)));
		++gid;
	}
	return ret;
}

static const struct vvideo_call_t vvideo_call_g_ext_ctrls = {
	.cmd = VIDIOC_G_EXT_CTRLS,
	.before = vvideo_thread_ext_ctrls_before,
	.after = vvideo_thread_ext_ctrls_after
};

static const struct vvideo_call_t vvideo_call_s_ext_ctrls = {
	.cmd = VIDIOC_S_EXT_CTRLS,
	.before = vvideo_thread_ext_ctrls_before,
	.after = vvideo_thread_ext_ctrls_after
};

static const struct vvideo_call_t vvideo_call_try_ext_ctrls = {
	.cmd = VIDIOC_TRY_EXT_CTRLS,
	.before = vvideo_thread_ext_ctrls_before,
	.after = vvideo_thread_ext_ctrls_after
};

static const struct vvideo_call_t *const vvideo_calls[] = {
	// struct v4l2_capability
	[_IOC_NR(VIDIOC_QUERYCAP)] = &vvideo_call_querycap,
	// struct v4l2_fmtdesc does not need processing
	// [_IOC_NR(VIDIOC_ENUM_FMT)] = &vvideo_call_enum_fmt,
	// struct v4l2_format does not need processing (spills into ext[])
	// [_IOC_NR(VIDIOC_G_FMT)] = &vvideo_call_g_fmt,
	// struct v4l2_format does not need processing (spills into ext[])
	// [_IOC_NR(VIDIOC_S_FMT)] = &vvideo_call_s_fmt,
	// struct v4l2_requestbuffers does not need processing
	// [_IOC_NR(VIDIOC_REQBUFS)] = &vvideo_call_reqbufs,
	// struct v4l2_buffer
	[_IOC_NR(VIDIOC_QUERYBUF)] = &vvideo_call_querybuf,
	// NOK: v4l2_framebuffer::base is not going to work
	// [_IOC_NR(VIDIOC_G_FBUF)] = &vvideo_call_g_fbuf,
	// NOK: v4l2_framebuffer::base is not going to work
	// [_IOC_NR(VIDIOC_S_FBUF)] = &vvideo_call_s_fbuf,
	// struct v4l2_buffer
	[_IOC_NR(VIDIOC_QBUF)] = &vvideo_call_qbuf,
	// struct v4l2_exportbuffer
	[_IOC_NR(VIDIOC_EXPBUF)] = &vvideo_call_expbuf,
	// struct v4l2_buffer
	[_IOC_NR(VIDIOC_DQBUF)] = &vvideo_call_dqbuf,
	// enum v4l2_buf_type
	[_IOC_NR(VIDIOC_STREAMON)] = &vvideo_call_streamon,
	// enum v4l2_buf_type
	[_IOC_NR(VIDIOC_STREAMOFF)] = &vvideo_call_streamoff,
	// struct v4l2_streamparm does not need processing (spills into ext[])
	// [_IOC_NR(VIDIOC_G_PARM)] = &vvideo_call_g_parm,
	// struct v4l2_streamparm does not need processing (spills into ext[])
	// [_IOC_NR(VIDIOC_S_PARM)] = &vvideo_call_s_parm,
	// struct v4l2_input does not need processing
	// [_IOC_NR(VIDIOC_ENUMINPUT)] = &vvideo_call_input,
	// struct v4l2_control
	[_IOC_NR(VIDIOC_G_CTRL)] = &vvideo_call_g_ctrl,
	// struct v4l2_control
	[_IOC_NR(VIDIOC_S_CTRL)] = &vvideo_call_s_ctrl,
	// struct v4l2_queryctrl does not need processing
	// [_IOC_NR(VIDIOC_QUERYCTRL)] = &vvideo_call_queryctrl,
	// struct v4l2_querymenu does not need processing
	// [_IOC_NR(VIDIOC_QUERYMENU)] = &vvideo_call_querymenu,
	// int does not need processing
	// [_IOC_NR(VIDIOC_G_INPUT)] = &vvideo_call_g_input,
	// int does not need processing
	// [_IOC_NR(VIDIOC_S_INPUT)] = &vvideo_call_s_input,
	// struct v4l2_edid
	[_IOC_NR(VIDIOC_G_EDID)] = &vvideo_call_g_edid,
	// struct v4l2_edid
	[_IOC_NR(VIDIOC_S_EDID)] = &vvideo_call_s_edid,
	// struct v4l2_cropcap does not need processing
	// [_IOC_NR(VIDIOC_CROPCAP)] = &vvideo_call_cropcap,
	// struct v4l2_crop does not need processing
	// [_IOC_NR(VIDIOC_G_CROP)] = &vvideo_call_g_crop,
	// struct v4l2_crop does not need processing
	// [_IOC_NR(VIDIOC_S_CROP)] = &vvideo_call_s_crop,
	// struct v4l2_format does not need processing
	// [_IOC_NR(VIDIOC_TRY_FMT)] = &vvideo_call_try_fmt,
	// enum v4l2_priority does not need processing
	// [_IOC_NR(VIDIOC_G_PRIORITY)] = &vvideo_call_g_priority,
	// enum v4l2_priority does not need processing
	// [_IOC_NR(VIDIOC_S_PRIORITY)] = &vvideo_call_s_priority,
	// struct v4l2_ext_controls
	[_IOC_NR(VIDIOC_G_EXT_CTRLS)] = &vvideo_call_g_ext_ctrls,
	// struct v4l2_ext_controls
	[_IOC_NR(VIDIOC_S_EXT_CTRLS)] = &vvideo_call_s_ext_ctrls,
	// struct v4l2_ext_controls
	[_IOC_NR(VIDIOC_TRY_EXT_CTRLS)] = &vvideo_call_try_ext_ctrls,
	// struct v4l2_frmsizeenum does not need processing
	// [_IOC_NR(VIDIOC_ENUM_FRAMESIZES)] = &vvideo_call_enum_framesizes,
	// struct v4l2_encoder_cmd does not need processing
	// [_IOC_NR(VIDIOC_ENCODER_CMD)] = &vvideo_call_encoder_cmd,
	// struct v4l2_encoder_cmd does not need processing
	// [_IOC_NR(VIDIOC_TRY_ENCODER_CMD)] = &vvideo_call_try_encoder_cmd,
	// struct v4l2_event does not need processing (spills into ext[])
	// [_IOC_NR(VIDIOC_DQEVENT)] = &vvideo_call_dqevent,
	// struct v4l2_event_subscription does not need processing
	// [_IOC_NR(VIDIOC_SUBSCRIBE_EVENT)] = &vvideo_call_subscribe_event,
	// struct v4l2_event_subscription does not need processing
	// [_IOC_NR(VIDIOC_UNSUBSCRIBE_EVENT)] = &vvideo_call_unsubscribe_event,
	// struct v4l2_buffer
	[_IOC_NR(VIDIOC_PREPARE_BUF)] = &vvideo_call_prepare_buf,
	// struct v4l2_selection does not need processing
	// [_IOC_NR(VIDIOC_G_SELECTION)] = &vvideo_call_g_selection,
	// struct v4l2_selection does not need processing
	// [_IOC_NR(VIDIOC_S_SELECTION)] = &vvideo_call_s_selection,
	// struct v4l2_decoder_cmd does not need processing
	// [_IOC_NR(VIDIOC_DECODER_CMD)] = &vvideo_call_decoder_cmd,
	// struct v4l2_decoder_cmd does not need processing
	// [_IOC_NR(VIDIOC_TRY_DECODER_CMD)] = &vvideo_call_try_decoder_cmd,
};

/*
 * Called on VVIDEO2_REQ_IOCTL.
 * vvideo_dev_t::lock must be Unlocked
 */

static VVIDEO_NOINLINE int vvideo_req_ioctl(struct vvideo_thread_t *t)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct file *file;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	const unsigned int cmd = req_ioctl->cmd;
	const struct vvideo_call_t *call = NULL;
	const unsigned int nr = _IOC_NR(cmd);
	int ret;

	VVIDEO_LOG("ctx %s %s, cmd %#x (%s)\n", ctx->name, t->name, cmd,
		   vvideo_ioctl_name(cmd));
	t->ioctl.cmd = cmd;
	/*
	 * Do not trust req_ioctl->arg_size, or the frontend could make
	 * backend follow an arbitrary pointer by setting arg_size 0 for
	 * an ioctl() which takes a pointer.
	 * t->ioctl.arg will be redirected to secure local copy for
	 * selected requests.
	 */
	t->ioctl.arg = _IOC_SIZE(cmd) ?
		(unsigned long) req_ioctl->arg : req_ioctl->arg_value;
	/* t->ioctl.fence_fd is only set by buffer handlers */

	/* Unlisted cmd values get no pre- or post-processing */
	if (nr < ARRAY_SIZE(vvideo_calls) && vvideo_calls[nr] &&
	    vvideo_calls[nr]->cmd == cmd)
		call = vvideo_calls[nr];
	if (call && call->before) {
		ret = call->before(t);
		if (ret)
			return ret;
	}
#ifdef VVIDEO_USE_FD
	file = fget(ctx->fd);
#else
	file = ctx->file;
#endif
	spin_lock(&file->f_lock);
	if (m->req_flags & VVIDEO2_REQ_BLOCK)
		file->f_flags &= ~O_NONBLOCK;
	else if (m->req_flags & VVIDEO2_REQ_NONBLOCK)
		file->f_flags |= O_NONBLOCK;
	spin_unlock(&file->f_lock);

#ifdef VVIDEO_USE_FD
	ret = t->be->sys_ioctl(ctx->fd, cmd, t->ioctl.arg);
#else
	if (ctx->file->f_op->unlocked_ioctl)
		ret = ctx->file->f_op->unlocked_ioctl(ctx->file, cmd,
						      t->ioctl.arg);
	else
		ret = -EINVAL;
#endif
	VVIDEO_LOG("ctx %s %s, cmd %#x (%s) DONE, ret %d\n", ctx->name,
		   t->name, cmd, vvideo_ioctl_name(cmd), ret);
	if (call && call->after)
		ret = call->after(t, ret);
#ifdef VVIDEO_USE_FD
	fput(file);
#endif
	return ret;
}

static VVIDEO_NOINLINE void vvideo_thread_set_msg(struct vvideo_thread_t *t,
	struct vvideo_msg_t *m);
/*
 * t->m and m->req must be always set.
 * m->dev is not always set, so cannot use it to get vl.
 * vvideo_thread_t::lock must be Unlocked.
 */

static VVIDEO_NOINLINE void vvideo_ack(struct vvideo_thread_t *t, int result)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_link_t *vl = m->vl;
	struct VVideo2Request *req = m->req;

	/* Make sure other people do not read t->m anymore */
	vvideo_thread_set_msg(t, NULL);
	/* t->m is NULL now */
	req->result = result;
	VVIDEO_LOG_COMMS(
		"%s, req %d (%s), trans %d, ctx %#x, slot %d, result %d\n",
		t->name, m->req_num, vvideo_req_full(req),
		req->transaction,
		m->be_context, vmq_msg_rx_slot(vl->link, req),
		req->result);
	if (m->req_flags & VVIDEO2_REQ_ASYNC) {
		++t->deferred_pending;
		/* Test for invalid be_context and VVIDEO2_REQ_ASYNC */
		if (m->dev)
			++m->dev->stats.deferred_returns;
		/* This just does not send xirq yet */
		vmq_msg_free(vl->link, req);
	} else {
		vmq_msg_return(vl->link, req);
	}
	/*
	 * Message at req is no more belonging to us now.
	 * m->dev, if any, is under kref and released separately.
	 */
}

static int vvideo_req_received(struct vvideo_thread_t *t)
{
	const struct vvideo_be_t *be = t->be;
	const int ret = be->receive_notify || be->ctx_put.notify ||
		atomic_read(&be->ctx_close_notify) > 0 ||
		be->sysconf_notify || t->aborted;

	VVIDEO_LOG_COMMS(
		"%s, receive %d ctx_put %d sysconf %d abort %d -> ret %d\n",
		 t->name, be->receive_notify, be->ctx_put.notify,
		 be->sysconf_notify, t->aborted, ret);
	return ret;
}

/*
 * Must be called from a vvideo_thread,
 * because it operates on file descriptors.
 * vvideo_dev_t::lock must be Locked
 */

static VVIDEO_NOINLINE void vvideo_dev_kref_release(struct kref *kref)
{
	struct vvideo_dev_t *vvideo_dev = container_of(kref,
						struct vvideo_dev_t, kref);

	if (vvideo_dev->reset_pending) {
		vvideo_dev_release_ctxs(vvideo_dev);
		vvideo_dev->reset_pending = false;
	}
}

/*
 * Must be called from a vvideo_thread,
 * because it operates on file descriptors.
 * vvideo_dev_t::lock must be Unlocked
 */

static VVIDEO_NOINLINE void vvideo_dev_release(struct vvideo_dev_t *vvideo_dev)
{
	VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&vvideo_dev->kref) > 0);
	/*
	 * kref_put_mutex() atomically locks vvideo_dev->lock
	 * before calling vvideo_dev_kref_release() on last put.
	 */
	if (kref_put_mutex(&vvideo_dev->kref, vvideo_dev_kref_release,
			   &vvideo_dev->lock))
		mutex_unlock(&vvideo_dev->lock);
}

/*
 * Used at vvideo_thread() termination and failed creation.
 * No work to do.
 * vvideo_be_t::lock is Locked
 */

static void vvideo_be_kref_idle_threads_dummy_release(struct kref *kref)
{
	(void)kref;
	VVIDEO_LOG("entered\n");
}

/*
 * Used at vvideo_thread() failed creation.
 * No work to do.
 * vvideo_be_t::lock is Locked
 */

static void vvideo_be_kref_running_threads_dummy_release(struct kref *kref)
{
	(void)kref;
	VVIDEO_LOG("entered\n");
}

static int vvideo_thread(void *data);

/* vvideo_be_t::lock must be Locked */

static VVIDEO_NOINLINE void vvideo_be_start_next_thread(struct vvideo_be_t *be)
{
	struct vvideo_thread_t *t;
	int t_slot;

	VVIDEO_FOR_ALL_THREADS(be, t_slot, t)
	{
		if (t->pid <= 0)
			break;
	}
	if (t_slot >= be->max_reqs) {
		++be->stats.max_threads;
		VVIDEO_LOG("Already max %d threads\n", be->max_reqs);
		return;
	}
	++be->stats.thread_starts;
	/*
	 * Threads can start before we had the chance to increment
	 * these counters, and idle_thread could get decremented
	 * below 0 when starting to process a request, so do increments
	 * before creating them.
	 */
	vvideo_kref_get_unlocked(&be->idle_threads);
	vvideo_kref_get_unlocked(&be->running_threads);
	if (be->stats.max_running < vvideo_kref_read(&be->running_threads))
		be->stats.max_running = vvideo_kref_read(&be->running_threads);
	/* Pass SIGCHLD to avoid zombies as we do not kernel_wait4() */
	t->pid = be->kernel_thread(vvideo_thread, t, CLONE_FS | CLONE_FILES |
							SIGCHLD);
	if (t->pid <= 0) {
		VVIDEO_ERR_LIMITED(be->stats.kernel_thread,
			"kernel_thread %s failed (%d)\n", t->name, t->pid);
		t->pid = 0;
		/*
		 * kref_put_mutex() does not need to be used here. Indeed,
		 * if creation fails for first thread, module initialization
		 * will fail. If it fails for subsequent threads, there is
		 * nothing we can do in idle_threads release handler, so no
		 * need to take a mutex. The running_threads can become 0
		 * only at startup, so kref_put() will never run handler
		 * in non-startup circumstances, hence no need for mutex.
		 */
		kref_put(&be->idle_threads,
			 vvideo_be_kref_idle_threads_dummy_release);
		kref_put(&be->running_threads,
			 vvideo_be_kref_running_threads_dummy_release);
	} else {
		VVIDEO_LOG_VERBOSE(
			"started %s, idle_threads %d, running_threads %d\n",
			   t->name, (int)vvideo_kref_read(&be->idle_threads),
			   (int)vvideo_kref_read(&be->running_threads));
	}
	VVIDEO_ASSERT_BE(vvideo_kref_read(&be->running_threads) >=
			 vvideo_kref_read(&be->idle_threads));
}

/* vvideo_be_t::lock must be Locked */

static VVIDEO_NOINLINE void vvideo_be_kref_idle_threads_release(
							struct kref *kref)
{
	struct vvideo_be_t *be = container_of(kref, struct vvideo_be_t,
					      idle_threads);

	VVIDEO_ASSERT_BE(vvideo_kref_read(&be->idle_threads) == 0);
	VVIDEO_ASSERT_BE(vvideo_kref_read(&be->running_threads) > 0);
	vvideo_be_start_next_thread(be);
}

/* vvideo_thread_t::lock must be Unlocked */

static VVIDEO_NOINLINE void vvideo_thread_set_msg(struct vvideo_thread_t *t,
	struct vvideo_msg_t *m)
{
	mutex_lock(&t->lock);
	/* Get rid of signals for previous messages */
	flush_signals(current);
	t->m = m;
	mutex_unlock(&t->lock);
}

/*
 * vvideo_thread_t::lock must be Unlocked for aborted thread
 */

static VVIDEO_NOINLINE int vvideo_req_abort(struct vvideo_thread_t *t1)
{
	struct vvideo_be_t *be = t1->be;
	struct vvideo_thread_t *t2;
	int			t2_slot;

	++be->stats.sig_processed;
	VVIDEO_LOG("%s\n", t1->name);
	VVIDEO_FOR_ALL_THREADS(be, t2_slot, t2)
	{
		if (t2 == t1)
			continue;
		mutex_lock(&t2->lock);
		if (!t2->m || t2->m->req->transaction !=
		    t1->m->req->u.abort.transaction) {
			mutex_unlock(&t2->lock);
			continue;
		}
		/*
		 * It only makes sense to send signals which we have
		 * earlier enabled with allow_signal(), that is SIGTERM.
		 * SIGINT or SIGKILL would be ignored.
		 */
		VVIDEO_LOG("%s sending SIGTERM to %s\n", t1->name, t2->name);
		++be->stats.sig_delivered;
		kill_pid(t2->struct_pid, SIGTERM, 1);
		mutex_unlock(&t2->lock);
		break;
	}
	VVIDEO_LOG("%s DONE\n", t1->name);
	return 0;
}

static void vvideo_poll_kref_release(struct kref *kref);

static VVIDEO_NOINLINE void vvideo_link_send_events(struct vvideo_link_t *vl)
{
	unsigned long spinlock_flags;

	spin_lock_irqsave(&vl->events.spinlock, spinlock_flags);
	while (!list_empty(&vl->events.list_head)) {
		struct VVideo2Request *req;
		struct vvideo_poll_t *poll;
		struct vvideo_dev_t *vvideo_dev;
		int ret;

		poll = container_of(vl->events.list_head.next,
			struct vvideo_poll_t, link_list);
		vvideo_dev = poll->ctx->dev;
		/* Must hold vl->events.spinlock to avoid lost returns */
		ret = vmq_msg_allocate_ex(vl->link, 0, (void **)&req, NULL,
					true);
		if (ret == -EAGAIN) {
			++vvideo_dev->stats.poll.eagain;
			break;
		}
		if (ret) {
			++vvideo_dev->errors.poll_error;
			break;
		}
		/* Detach from vl->events.list_head */
		/* This makes "poll" not (kref C) cancellable */
		list_del_init(&poll->link_list);
		spin_unlock_irqrestore(&vl->events.spinlock, spinlock_flags);

		VVIDEO_ZERO(req);
		req->req_num = VVIDEO2_REQ_EVENT;
		req->be_context = poll->ctx->be_context;
		req->u.poll.events = poll->revents;
		req->u.poll.cookie = poll->cookie;
		vmq_msg_send(vl->link, req);
		++vvideo_dev->stats.poll.event;
		/*
		 * Event send was successful for this poll.
		 * (kref C) put, which could release ctx->kref_used.
		 */
		VVIDEO_LOG("poll %s revents %#x kref %d\n", poll->name,
			poll->revents, (int) vvideo_kref_read(&poll->kref));
		VVIDEO_ASSERT(vl->be, vvideo_kref_read(&poll->kref) > 0);
		kref_put(&poll->kref, vvideo_poll_kref_release);

		spin_lock_irqsave(&vl->events.spinlock, spinlock_flags);
	}
	spin_unlock_irqrestore(&vl->events.spinlock, spinlock_flags);
}

/*
 * Remove (kref A) and (kref B). Remove (kref C) if event_send_too.
 * Caller must have a (kref D).
 */
static VVIDEO_NOINLINE void vvideo_poll_cancel(struct vvideo_poll_t *poll,
			_Bool event_send_too)
{
	struct vvideo_ctx_t *ctx = poll->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	unsigned long spinlock_flags;
	_Bool cookie_valid;
	_Bool send_pending;
#ifndef VVIDEO_POLL_WQUEUES
	unsigned int i;
#endif

	VVIDEO_LOG("poll %s spinlock %lx kref %d\n",
		    poll->name, *(long *) &ctx->poll.spinlock,
		    vvideo_kref_read(&poll->kref));
	/*
	 * (kref A) put the kref from 1st VVIDEO2_REQ_POLL.
	 *  "poll" has it if poll->cookie is not 0.
	 */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	cookie_valid = poll->cookie != 0;
	poll->cookie = 0;
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
	if (cookie_valid) {
		VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&poll->kref) >= 2);
		/* (kref A) put. (kref D) still held */
		kref_put(&poll->kref, vvideo_poll_kref_release);
	}
	/*
	 * (kref B) put. Remove all wakeup handlers.
	 * They exist if "wqh" is not NULL.
	 * remove_wait_queue() locks the queue temporarily,
	 * while __remove_wait_queue() does not.
	 */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
#ifndef VVIDEO_POLL_WQUEUES
	VVIDEO_LOG("poll %s index %d\n", poll->name, poll->index);
	for (i = 0; i < poll->index; ++i) {
		struct vvideo_poll_table_entry_t *entry = poll->entries + i;
		wait_queue_head_t *wqh;

		VVIDEO_LOG("i %d entry %pS\n", i, entry);
		wqh = entry->wqh;
		entry->wqh = NULL;
		if (!wqh)
			continue;
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		VVIDEO_LOG(
	    "wq: flags %#x private %pS func %ps entry %pS next %pS prev %pS\n",
			entry->wq.flags, entry->wq.private, entry->wq.func,
			vvideo_wait_queue_entry_list(&entry->wq),
			vvideo_wait_queue_entry_list(&entry->wq)->next,
			vvideo_wait_queue_entry_list(&entry->wq)->prev);
		remove_wait_queue(wqh, &entry->wq);
		/* remove_wait_queue() calls do not reset the list_head */
		INIT_LIST_HEAD(vvideo_wait_queue_entry_list(&entry->wq));
		/* (kref B) put. (kref D) still held */
		VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&poll->kref) >= 2);
		kref_put(&poll->kref, vvideo_poll_kref_release);
		spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	}
#endif
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
	if (!event_send_too)
		return;
	/* (kref C) Remove from send queue */
	VVIDEO_LOG("ctx kref_used %d\n", vvideo_kref_read(&ctx->kref_used));
	spin_lock_irqsave(&vl->events.spinlock, spinlock_flags);
	/* Detach from vl->events.list_head */
	send_pending = !list_empty(&poll->link_list);
	if (send_pending)
		list_del_init(&poll->link_list);
	spin_unlock_irqrestore(&vl->events.spinlock, spinlock_flags);
	if (send_pending) {
		VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&poll->kref) >= 2);
		/* (kref C) put. (kref D) still held */
		kref_put(&poll->kref, vvideo_poll_kref_release);
	}
	VVIDEO_LOG("poll kref %d ctx kref_busy %d\n",
		(int) vvideo_kref_read(&poll->kref),
		vvideo_kref_read(&ctx->kref_busy));
}

/*
 * This is a wait_queue_func_t.
 * The wait_queue_head_t lock is Locked.
 * "poll" associated with wq is on ctx->poll.list_head.
 * In VVIDEO_POLL_WQUEUES case, this function completely replaces
 * pollwake() from fs/select.c because we do not have several
 * files to handle and no thread to wakeup.
 */

static VVIDEO_NOINLINE int vvideo_poll_wakeup(VVIDEO_WAIT_QUEUE_ENTRY *wq,
			unsigned int mode, int sync, void *key)
{
	struct vvideo_poll_t *poll = wq->private;
#ifdef VVIDEO_POLL_WQUEUES
	struct poll_table_entry *entry =
		container_of(wq, struct poll_table_entry, wait);
	int index = entry - poll->poll_wqueues.inline_entries;
#else
	struct vvideo_poll_table_entry_t *entry =
		container_of(wq, struct vvideo_poll_table_entry_t, wq);
	int index = entry - poll->entries;
#endif
	struct vvideo_ctx_t *ctx = poll->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	unsigned long poll_key = (unsigned long) key;
	unsigned long spinlock_flags;

	(void) index;
	VVIDEO_LOG(
	"poll %s index %d mode %#x sync %#x key %#lx mask %#llx kref %d\n",
		poll->name, index, mode, sync, poll_key,
		poll->mask, (int) vvideo_kref_read(&poll->kref));
	/* A NULL key matches any poll->mask */
	if (poll_key) {
		++vvideo_dev->stats.poll.key_set;
		if (!(poll_key & poll->mask)) {
			++vvideo_dev->stats.poll.no_event;
			VVIDEO_LOG("poll %s no events\n", poll->name);
			return 0;
		}
	} else {
		++vvideo_dev->stats.poll.key_null;
	}
	/* poll can have "wakeups", but not be on event list yet or anymore */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	/*
	 * Only send an event for first NULL key or if new event bits
	 * are added.
	 */
	if ((!poll_key && poll->wakeups) ||
	     (poll_key && !(poll_key & ~poll->revents))) {
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		++vvideo_dev->stats.poll.dup;
		return 1;
	}
	++poll->wakeups;
	poll->revents |= poll_key;
	/* Make sure all new revents are sent out */
	++vvideo_dev->stats.poll.wakeup;
	/*
	 * (kref D) get. This prevents removal while we are adding
	 * "poll" to event list with spinlocks released.
	 */
	kref_get(&poll->kref);
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);

	spin_lock_irqsave(&vl->events.spinlock, spinlock_flags);
	/*
	 * If list_empty, "poll" is either not yet being sent, or
	 * already being sent. In both cases, we add it, or re-add
	 * it, on event list, so that latest poll->revents are sent
	 * eventually. They are only reset at next vvideo_req_poll().
	 */
	if (list_empty(&poll->link_list)) {
		/* (kref C) get */
		kref_get(&poll->kref);
		VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&poll->kref) >= 2);
		list_add_tail(&poll->link_list, &vl->events.list_head);
	}
	spin_unlock_irqrestore(&vl->events.spinlock, spinlock_flags);
	/* "poll" is not necessarily first on event list */
	vvideo_link_send_events(vl);
	/* (kref D) put */
	kref_put(&poll->kref, vvideo_poll_kref_release);
	/* "poll" could be no more valid here */
	return 1;
}

/* Call-back from native driver's file::f_op::poll() */
/* (kref A) from vvideo_req_poll() is held */

static VVIDEO_NOINLINE void vvideo_poll_wait(struct file *file,
				wait_queue_head_t *wqh, poll_table *poll_table)
{
#ifdef VVIDEO_POLL_WQUEUES
	struct vvideo_poll_t *poll =
		container_of(poll_table, struct vvideo_poll_t, poll_wqueues.pt);
	struct poll_wqueues *pwq =
		container_of(poll_table, struct poll_wqueues, pt);
	struct poll_table_entry *entry =
		pwq->inline_entries + pwq->inline_index;
#else
	struct vvideo_poll_t *poll =
		container_of(poll_table, struct vvideo_poll_t, poll_table);
	struct vvideo_ctx_t *ctx = poll->ctx;
	struct vvideo_poll_table_entry_t *entry;
	unsigned long spinlock_flags;
#endif

	VVIDEO_LOG("poll %s _qproc %ps _key %#lx\n", poll->name,
		poll_table->_qproc, (long) poll_table->_key);
#ifdef VVIDEO_POLL_WQUEUES
	/* (kref B) get. One invocation per add_wait_queue() */
	kref_get(&poll->kref);
	poll_wait(file, wqh, poll_table);	/* void */
	/* Change wait's function and set our cookie */
	init_waitqueue_func_entry(&entry->wait, vvideo_poll_wakeup);
	entry->wait.private = poll;
#else
	/*
	 * Native driver is allowed to call poll_wait()
	 * VVIDEO_POLL_MAX times at most. This limitation does
	 * not exist with poll_wait(), which allocates more
	 * memory dynamically.
	 */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	if (poll->index >= VVIDEO_POLL_MAX) {
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.poll.max,
			"device %s Too many simultaneous poll_wait\n",
			ctx->dev->name);
		return;
	}
	entry = poll->entries + poll->index++;
	VVIDEO_LOG("poll %s index %d\n", poll->name, poll->index);
	init_waitqueue_func_entry(&entry->wq, vvideo_poll_wakeup);
	entry->wq.private = poll;
	/*
	 * We must not set entry->wqh yet, because vvideo_poll_cancel()
	 * could do cleanup, and remove_wait_queue() before we called
	 * add_wait_queue().
	 */
	entry->wqh = NULL;
	/* (kref B) get. One invocation per add_wait_queue() */
	kref_get(&poll->kref);
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
	/*
	 * We must release the context spinlock before add_wait_queue().
	 * Indeed, add_wait_queue() allows immediate invocation of
	 * vvideo_poll_wakeup() handler, which also will take the same
	 * context lock.
	 */
	add_wait_queue(wqh, &entry->wq);

	/* Allow cleanup by vvideo_poll_cancel() */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	entry->wqh = wqh; /* Allow entry->wq removal */
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);

	VVIDEO_LOG(
	    "wq: flags %#x private %pS func %ps entry %pS next %pS prev %pS\n",
		entry->wq.flags, entry->wq.private, entry->wq.func,
		vvideo_wait_queue_entry_list(&entry->wq),
		vvideo_wait_queue_entry_list(&entry->wq)->next,
		vvideo_wait_queue_entry_list(&entry->wq)->prev);
#endif
}

/* ctx->poll.spinlock must be Unlocked */

static VVIDEO_NOINLINE void vvideo_poll_init(struct vvideo_poll_t *poll,
			struct vvideo_ctx_t *ctx, unsigned long long events,
			unsigned int cookie, unsigned int does_not_wait,
			_Bool has_list)
{
	unsigned long spinlock_flags;

	kref_get(&ctx->kref_used);
	poll->ctx = ctx;
#if defined VVIDEO_DEBUG || defined VVIDEO_VERBOSE
	snprintf(poll->name, sizeof(poll->name), "%s.%x", ctx->name, cookie);
#endif
#ifdef VVIDEO_POLL_WQUEUES
	poll_initwait(&poll->poll_wqueues);
	/*
	 * poll_initwait() has set the  __pollwait() function,
	 * which is not suitable, as it deals with several files
	 * and keys and especially uses pollwake()), so override it.
	 */
#else
	poll->index = 0;
	/* poll->entries[] set by native driver with poll_wait() */
#endif
	init_poll_funcptr(&VVIDEO_POLL_TABLE(poll), vvideo_poll_wait);
	/* init_poll_funcptr() has set _key as ~0; make it truer looking */
	VVIDEO_POLL_TABLE(poll)._key = events;
	poll->mask = events;
	poll->cookie = cookie;
	poll->wakeups = 0;
	poll->revents = 0;
	if (does_not_wait)
		++ctx->dev->stats.poll.no_wait;
	else
		++ctx->dev->stats.poll.wait;
	INIT_LIST_HEAD(&poll->ctx_list);
	INIT_LIST_HEAD(&poll->link_list);
	/* (kref A) get reference till 2nd VVIDEO2_REQ_POLL */
	kref_init(&poll->kref);
	/* (kref D) get reference till current request terminates */
	kref_get(&poll->kref);

	/* Attach to ctx->poll.list_head */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	list_add_tail(&poll->ctx_list, &ctx->poll.list_head);
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
	/* (kref A) could be already removed by another FE req_poll */
}

static noinline void vvideo_poll_kref_release(struct kref *kref)
{
	struct vvideo_poll_t *poll =
		container_of(kref, struct vvideo_poll_t, kref);
	struct vvideo_ctx_t *ctx = poll->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	struct vvideo_be_t *be = vl->be;
	unsigned long spinlock_flags;
#ifndef VVIDEO_POLL_WQUEUES
	unsigned int i;
#endif

	VVIDEO_LOG("poll %s ctx kref_used %d poll kref %d\n",
		poll->name, vvideo_kref_read(&ctx->kref_used),
		(int) vvideo_kref_read(&poll->kref));
	VVIDEO_ASSERT(vvideo_dev, !list_empty(&poll->ctx_list));
	VVIDEO_ASSERT(vvideo_dev, list_empty(&poll->link_list));
#ifdef VVIDEO_POLL_WQUEUES
	poll_freewait(&poll->poll_wqueues);
#else
	for (i = 0; i < poll->index; ++i) {
		VVIDEO_ASSERT(vvideo_dev, !poll->entries[i].wqh);
		VVIDEO_ASSERT(vvideo_dev,
		list_empty(vvideo_wait_queue_entry_list(&poll->entries[i].wq)));
	}
#endif
	VVIDEO_ASSERT(ctx->dev, vvideo_kref_read(&ctx->kref_used) > 0);
	/* (kref D) put */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	list_del_init(&poll->ctx_list);
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);

	/* We cannot kref_put(ctx) from an interrupt handler */
	spin_lock_irqsave(&be->ctx_put.spinlock, spinlock_flags);
	if (list_empty(&ctx->kref_put.list_head))
		list_add_tail(&ctx->kref_put.list_head, &be->ctx_put.list_head);
	++ctx->kref_put.counter;
	spin_unlock_irqrestore(&be->ctx_put.spinlock, spinlock_flags);

	be->ctx_put.notify = true;
	wake_up(&be->wait);
#ifdef VVIDEO_DEBUG
	VVIDEO_ZERO(poll);
#endif
	kmem_cache_free(vl->be->poll_kmem_cache, poll);
}

/*
 * ctx->poll.spinlock must be Unlocked.
 * vl->events.spinlock must be Unlocked.
 */
static VVIDEO_NOINLINE void vvideo_ctx_cancel_polls(struct vvideo_ctx_t *ctx)
{
	unsigned long spinlock_flags;

	VVIDEO_LOG("ctx kref_used %d\n", vvideo_kref_read(&ctx->kref_used));
	/*
	 * Empty the ctx->poll.list_head and all applicable
	 * vl->events.list_head entries.
	 * (kref A) put. Do not wait for 2nd VVIDEO2_REQ_POLL.
	 * (kref B) put. Cancel callbacks.
	 * (kref C) put. Remove all ctx event list entries otherwise.
	 */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	while (!list_empty(&ctx->poll.list_head)) {
		struct vvideo_poll_t *poll =
			container_of(ctx->poll.list_head.next,
			struct vvideo_poll_t, ctx_list);

		++ctx->dev->stats.poll.discard;
		/* (kref D) get */
		kref_get(&poll->kref);
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		vvideo_poll_cancel(poll, true /*event_send_too*/);
		/* (kref D) put */
		kref_put(&poll->kref, vvideo_poll_kref_release);
		/* "poll" could be no more valid here */
		spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	}
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
	VVIDEO_LOG("ctx kref_used %d\n", vvideo_kref_read(&ctx->kref_used));
}

/* vvideo_dev_t::lock must be Unlocked */

static VVIDEO_NOINLINE int vvideo_req_poll(struct vvideo_thread_t *t)
{
	struct vvideo_msg_t *m = t->m;
	struct vvideo_ctx_t *ctx = m->ctx;
#ifdef VVIDEO_USE_FD
	struct file *file = fget(ctx->fd);
#else
	struct file *file = ctx->file;
#endif
	struct VVideo2Poll *req_poll = &m->req->u.poll;
	const unsigned long long events = req_poll->events;
	const unsigned int cookie = req_poll->cookie;
	const unsigned int does_not_wait = req_poll->does_not_wait;
	const _Bool has_list = req_poll->has_list;
	unsigned int mask = 0;
	struct vvideo_poll_t *spare;
	struct vvideo_poll_t *poll = NULL;
	unsigned long spinlock_flags;
	int polls;

	VVIDEO_LOG("%s poll %s.%x events %#llx does_not_wait %d has_list %d\n",
		t->name, ctx->name, cookie, events, does_not_wait, has_list);
	if (!file->f_op->poll) {
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.poll.f_op,
			"poll %s.%x no f_op\n", ctx->name, cookie);
		goto exit;
	}
	if (!cookie) {
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.poll.cookie,
			"ctx %s Invalid null cookie\n", ctx->name);
		goto exit;
	}
	spare = kmem_cache_alloc(ctx->dev->link->be->poll_kmem_cache,
		GFP_ATOMIC);
	polls = 0;
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	list_for_each_entry(poll, &ctx->poll.list_head, ctx_list) {
		++polls;
		if (poll->cookie == cookie)
			break;
	}
	/* "poll" is never NULL */
	if (&poll->ctx_list != &ctx->poll.list_head) {
		/* Found "cookie" in list at "poll" */
		VVIDEO_POLL_TABLE(poll)._qproc = NULL;
		/* Re-arm sending of events on new wake-ups */
		poll->wakeups = 0;
		poll->revents = 0;
		/* (kref D) get. Keep "poll" available for us */
		kref_get(&poll->kref);
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		VVIDEO_LOG("poll %s 2nd call\n", poll->name);
		kmem_cache_free(ctx->dev->link->be->poll_kmem_cache, spare);
		if (!does_not_wait) {
			VVIDEO_WARN_LIMITED(ctx->dev->warnings.poll.dup_wait,
				"ctx %s 2nd wait on same cookie\n", ctx->name);
			goto exit;
		}
		++ctx->dev->stats.poll.second;
	} else if (polls > VVIDEO_CTX_POLL_MAX) {
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		/* We did not take (kref D) so do not kref_put() */
		if (spare)
			kmem_cache_free(ctx->dev->link->be->poll_kmem_cache,
				spare);
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.poll.too_many,
			"poll %s.%x too many running (%d)\n",
				ctx->name, cookie, VVIDEO_CTX_POLL_MAX);
		poll = NULL;
		goto exit;
	} else if (spare) {
		/* Maybe should hold lock to avoid duplicate cookies? */
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		poll = spare;
		/* This attaches "poll" to ctx->poll.list_head */
		vvideo_poll_init(poll, ctx, events, cookie, does_not_wait,
			has_list);
		VVIDEO_LOG("poll %s 1st call\n", poll->name);
		/* (kref A) could have been removed already */
	} else {
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.kmem_cache_alloc,
			"poll %s.%x alloc failed\n", ctx->name, cookie);
		poll = NULL;
		goto exit;
	}
	/*
	 * If has_list=0, remove wakeup handlers before calling
	 * f_op->poll(), rather than after, so that it gets the
	 * same environment as natively.
	 */
	if (!has_list) {
		++ctx->dev->stats.poll.no_list;
		/* (kref A) put */
		/* (kref B) put all */
		/* (kref C) keep, because maybe being sent */
		/* (kref D) keep (till exit from here) */
		vvideo_poll_cancel(poll, false /*!event_send_too*/);
	}
	/*
	 * A wait_queue_head_t is given to us through poll_wait(),
	 * to which we can attach our wait queue element,
	 * awaken when driver calls wake_up(queue-head).
	 *	void poll_wait(file, poll_table, wait);
	 */
	mask = file->f_op->poll(file, &VVIDEO_POLL_TABLE(poll));
	/*
	 * At exit, we may have some wait queue heads attached.
	 * It is also possible that the wake-up handler has already
	 * been called and has removed (kref A), (kref B) and (kref C).
	 */
	if (mask)
		++ctx->dev->stats.poll.mask;
	VVIDEO_LOG(
		"poll %s mask %#x _qproc %ps wakeups %d revents %#x kref %d\n",
		poll->name, mask, VVIDEO_POLL_TABLE(poll)._qproc, poll->wakeups,
		poll->revents, (int) vvideo_kref_read(&poll->kref));
exit:
#ifdef VVIDEO_USE_FD
	fput(file);
#endif
	/* Macro must not reference "poll", which can be invalid/NULL */
	VVIDEO_LOG("%s poll %s.%x DONE mask %#x\n",
		t->name, ctx->name, cookie, mask);
	/* (kref D) put, if any */
	if (poll)
		kref_put(&poll->kref, vvideo_poll_kref_release);
	return mask;
}

/*
 * Returns true if msg was appended to existing async_queue
 * and will be processed by initial thread.
 * vvideo_link_t::lock is Locked, but released during ops.
 * vvideo_dev_t::lock must be Unlocked.
 * vvideo_thread_t::lock must be Unlocked.
 */
static VVIDEO_NOINLINE _Bool vvideo_thread_enqueue_msg(
						struct vvideo_thread_t *t)
{
	struct vvideo_msg_t *m = t->m;

	mutex_lock(&m->dev->lock);
	while (m->ctx->async_queue) {
		/*
		 * If last msg is async, append and go away.
		 * Otherwise do not append and wait until the
		 * last async finishes processing. Do not
		 * wait for the sync msg itself to finish,
		 * as it can block for a long time, and this
		 * is not semantically required either.
		 */
		struct vvideo_msg_t **next = &m->ctx->async_queue;
		DECLARE_WAITQUEUE(wait, current);

		/* First message cannot be blocking */
		VVIDEO_ASSERT(m->dev,
			(*next)->req_flags & VVIDEO2_REQ_ASYNC);
		/* Find last waiting msg as (*next) */
		while ((*next)->next)
			next = &(*next)->next;
		if ((*next)->req_flags & VVIDEO2_REQ_ASYNC) {
			++m->dev->stats.behind_async;
			/* "m" can be async or blocking */
			(*next)->next = m;
			mutex_unlock(&m->dev->lock);
			vvideo_thread_set_msg(t, NULL);
			/* Do not vvideo_dev_release(m->dev) */
			/* Keep vvideo_link_t::lock */
			return true;
		}
		++m->dev->stats.wait_async;
		/* Wait until async messages finish processing */
		set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&m->ctx->async_wait, &wait);
		mutex_unlock(&m->dev->lock);
		/* Let other threads deal with other msgs */
		mutex_unlock(&m->vl->lock);
		schedule();
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&m->ctx->async_wait, &wait);
		if (signal_pending(current)) {
			++m->dev->stats.wait_signal;
			flush_signals(current);
		}
		mutex_lock(&m->vl->lock);
		/* Protects m->ctx->async_queue */
		mutex_lock(&m->dev->lock);
	}
	/* Take the lead */
	if (m->req_flags & VVIDEO2_REQ_ASYNC)
		m->ctx->async_queue = m;
	mutex_unlock(&m->dev->lock);
	return false;
}

static int (*const vvideo_reqs[]) (struct vvideo_thread_t *t) = {
	/* New context, if any, needs releasing */
	[VVIDEO2_REQ_OPEN] = vvideo_req_open,
	[VVIDEO2_REQ_RELEASE] = vvideo_req_release,
	[VVIDEO2_REQ_IOCTL] = vvideo_req_ioctl,
	[VVIDEO2_REQ_ABORT] = vvideo_req_abort,
	[VVIDEO2_REQ_POLL] = vvideo_req_poll
};

/*
 * vvideo_thread_t::m must be set.
 * vvideo_thread_t::lock must be Unlocked.
 * vvideo_dev_t::lock must be Unlocked.
 * vvideo_link_t::lock must be Unlocked as we can sleep.
 */
static VVIDEO_NOINLINE void vvideo_thread_process_msgs(
						struct vvideo_thread_t *t)
{
	struct vvideo_msg_t *m;
	int ret;

	while (1) {
		struct vvideo_msg_t *next;

		m = t->m;
		if (m->req_num >= ARRAY_SIZE(vvideo_reqs) ||
		    !vvideo_reqs[m->req_num]) {
			++m->dev->errors.req_unknown;
			ret = -EINVAL;
		} else {
			ret = vvideo_reqs[m->req_num](t);
		}
		VVIDEO_LOG(
		"%s, done req %d (%s), ctx %#x, slot %d, result %d idle %d\n",
			t->name, m->req_num, vvideo_req_full(m->req),
			m->be_context, vmq_msg_rx_slot(m->vl->link, m->req),
			ret, (int) vvideo_kref_read(&t->be->idle_threads));
		/*
		 * Check if need to process another queued msg.
		 * This is only the case if current msg was async,
		 * and if we got a ctx at all (not the case if open failed).
		 */
		if (!m->ctx)
			break;
		if (m->req_num == VVIDEO2_REQ_RELEASE && !ret) {
			struct vvideo_dev_t *vvideo_dev = m->dev;

			vvideo_thread_set_msg(t, NULL);
			m->req->result = ret;
			/*
			 * Once we attach "m" to ctx->release_msgs list,
			 * it could be recycled at vvideo_ctx_release(),
			 * so we must not use m->dev afterwards because
			 * it will be reassigned to a new device.
			 * We should also not clear m->ctx, because it
			 * will be reassigned too.
			 * Do not clear m->dev and m->ctx beforehand
			 * either, as vvideo_ctx_send_release_msgs()
			 * will check they are still OK.
			 */
			mutex_lock(&vvideo_dev->lock);
			++vvideo_dev->stats.release_msg;
			m->next = m->ctx->release_msgs;
			m->ctx->release_msgs = m;
			mutex_unlock(&vvideo_dev->lock);
			vvideo_ctx_release(m->ctx, true);
			/* "m" does not belong to us anymore */
			vvideo_dev_release(vvideo_dev);
			return;
		}
		if (!(m->req_flags & VVIDEO2_REQ_ASYNC))
			break;
		/* Current msg is the first one queued */
		VVIDEO_ASSERT(m->dev, m->ctx->async_queue == m);
		mutex_lock(&m->dev->lock);
		/* Remove current msg */
		next = m->ctx->async_queue = m->ctx->async_queue->next;
		/*
		 * If next msg exists and is no more async, remove it too
		 * and wake up other threads.
		 */
		if (!next) {
			++m->dev->stats.next_none;
		} else if (next->req_flags & VVIDEO2_REQ_ASYNC) {
			++m->dev->stats.next_async;
		} else {
			++m->dev->stats.next_sync;
			m->ctx->async_queue = next->next;
			/* Queue should be empty now */
			VVIDEO_ASSERT(m->dev, !m->ctx->async_queue);
			wake_up_all(&m->ctx->async_wait);
		}
		mutex_unlock(&m->dev->lock);
		if (!next)
			break;
		vvideo_ctx_release(m->ctx, true);
		m->ctx = NULL;
		vvideo_dev_release(m->dev);
		m->dev = NULL;
		/* This clears t->m */
		vvideo_ack(t, ret);
		/* "m" does not belong to us anymore */
		vvideo_thread_set_msg(t, next);
	}
	/* m->ctx is NULL for failed VVIDEO2_REQ_OPEN */
	if (m->ctx) {
		vvideo_ctx_release(m->ctx, true);
		m->ctx = NULL;
	}
	vvideo_dev_release(m->dev);
	m->dev = NULL;
	/* This clears t->m */
	vvideo_ack(t, ret);
	/* "m" does not belong to us anymore */
}

static _Bool vvideo_dev_stale_be_context(struct vvideo_dev_t *vvideo_dev,
					unsigned int be_context)
{
	/*
	 * Once last_be_context becomes larger than reserved space
	 * in be_context, all not found contexts will be "stale", and
	 * when it overflows, all not found contexts will be "invalid"
	 * for a short moment.
	 */
	return (be_context >> vvideo_dev->link->dev_width) <=
		vvideo_dev->last_be_context;
}

/*
 * vvideo_link_t::lock is Locked, but released during ops.
 * Thread must be "working".
 * vvideo_thread_t::lock must be Unlocked.
 * vvideo_dev_t::lock must be Unlocked.
 * vvideo_be_t::lock must be Unlocked.
 */

static VVIDEO_NOINLINE void vvideo_thread_req(struct vvideo_thread_t *t,
	struct vvideo_link_t *vl, struct VVideo2Request *req)
{
	struct vvideo_msg_t *m = vl->msgs + vmq_msg_rx_slot(vl->link, req);
	struct vvideo_be_t *be = t->be;
	int ret;

#ifdef VVIDEO_REMEMBER_TRANSACTIONS
	vl->transactions[vl->next_transaction++ % VVIDEO_LAST_TRANSACTIONS] =
		req->transaction;
#endif
	VVIDEO_ASSERT(be, !m->vl  || m->vl  == vl);
	VVIDEO_ASSERT(be, !m->req || m->req == req);
	m->vl = vl;	/* Will not change when msg reused */
	m->req = req;	/* Will not change when msg reused */
	/* For open request, minor is stored in be_context */
	m->req_num = req->req_num;
	m->req_flags = req->flags;
	m->be_context = req->be_context;
	m->dev = vvideo_link_find_dev(vl, m->be_context);
	m->ctx = NULL;
	m->next = NULL;
	vvideo_thread_set_msg(t, m);
	if (!m->dev) {
		VVIDEO_ERR_LIMITED(be->stats.invalid_dev,
			"%s, be_context %#x not found for link %s\n",
			   t->name, m->be_context, vl->name);
		ret = -ENOENT;
		goto exit;
	}
	VVIDEO_LOG_COMMS(
		"%s, req %d (%s), trans %d, ctx %#x, slot %d, result %#x\n",
			 t->name, m->req_num, vvideo_req_full(m->req),
			 m->req->transaction, m->be_context,
			 vmq_msg_rx_slot(vl->link, m->req), m->req->result);
	/*
	 * Releasing last m->dev->kref reference is a long running
	 * process which involves taking m->dev->lock mutex.
	 * During this time, kref remains at 0 and kref_get() might
	 * not increment it. We must wait until release has finished,
	 * if one is ongoing, by taking the mutex.
	 */
	vvideo_kref_get_mutex(&m->dev->kref, &m->dev->lock);

	/* For non-open requests, find context */
	if (m->req_num != VVIDEO2_REQ_OPEN) {
		    /* vvideo_dev_t::lock must be Unlocked */
		m->ctx = vvideo_dev_find_be_context(m->dev, m->be_context);
		if (!m->ctx) {
			if (m->req_num == VVIDEO2_REQ_ABORT) {
				/* Aborts are async versus release,
				 * so this is normal.
				 */
				++m->dev->stats.stale_abort;
				VVIDEO_LOG("%s invalid context %#x for ABORT\n",
					t->name, m->be_context);
			} else if (vvideo_dev_stale_be_context(m->dev,
				 m->be_context))
				++m->dev->stats.stale_ctx;
			else
				VVIDEO_ERR_LIMITED(
					m->dev->errors.invalid_context,
					"%s invalid context %#x req %s\n",
					t->name, m->be_context,
					vvideo_req_full(m->req));
			ret = -ESTALE;
			vvideo_dev_release(m->dev);
			m->dev = NULL;
			goto exit;
		}
	}
	VVIDEO_LOG_VERBOSE("%s, running/idle %d/%d\n", t->name,
		(int) vvideo_kref_read(&be->running_threads),
		(int) vvideo_kref_read(&be->idle_threads));
	VVIDEO_ASSERT_BE(vvideo_kref_read(&be->running_threads) >=
		vvideo_kref_read(&be->idle_threads));

	if (m->ctx && vvideo_thread_enqueue_msg(t))
		return;
	mutex_unlock(&vl->lock);

	/* Decrement idle_threads and start another thread if zero */
	if (kref_put_mutex(&be->idle_threads,
			   vvideo_be_kref_idle_threads_release, &be->lock))
		mutex_unlock(&be->lock);
	/* Wake up another thread in case we block */
	be->receive_notify = true;
	vl->receive_notify = true;
	wake_up(&be->wait);

	vvideo_thread_process_msgs(t);
	vvideo_kref_get_mutex(&be->idle_threads, &be->lock); /* Increment */
	mutex_lock(&vl->lock);
	return;
exit:
	mutex_unlock(&vl->lock);
	/* This clears t->m */
	vvideo_ack(t, ret);
	/* "m" does not belong to us anymore */
	mutex_lock(&vl->lock);
}

/* All locks are Unlocked on entry */

static VVIDEO_NOINLINE _Bool vvideo_link_receive_notify(vmq_link_t *link2,
							void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_thread_t *t = cookie;
	struct VVideo2Request *req;
	/*
	 * vl->receive_notify is not a counter of received
	 * and still pending messages. It only signals the
	 * arrival of the first message (empty to non-empty
	 * transition).
	 */
	if (!vl->receive_notify)
		return false;
	vl->receive_notify = false;
	VVIDEO_LOG_COMMS("%s, link %s\n", t->name, vl->name);
	/*
	 * Must serialize reception of link msgs.
	 * Do not allow another thread to retrieve the next
	 * msg until we decided how to process the current one.
	 * Indeed, reception of an async msg defers processing
	 * of subsequent requests on same context.
	 */
	mutex_lock(&vl->lock);
	while (!vmq_msg_receive(vl->link, (void **)&req))
		vvideo_thread_req(t, vl, req);
	mutex_unlock(&vl->lock);
	if (t->deferred_pending) {
		t->deferred_pending = 0;
		vmq_msg_return_flush(vl->link);
	}
	VVIDEO_LOG_VERBOSE("%s, link %s vl->receive_notify %d\n", t->name,
		   vl->name, vl->receive_notify);
	return false;
}

/*
 * All deferred kref_put(ctx) triggered from potential irq handlers.
 * vvideo_be_t::ctx_put.spinlock must be Unlocked.
 */

static VVIDEO_NOINLINE void vvideo_be_ctx_put(struct vvideo_be_t *be)
{
	unsigned long spinlock_flags;

	spin_lock_irqsave(&be->ctx_put.spinlock, spinlock_flags);
	while (!list_empty(&be->ctx_put.list_head)) {
		struct vvideo_ctx_t *ctx;
		int counter;

		ctx = container_of(be->ctx_put.list_head.next,
			struct vvideo_ctx_t, kref_put.list_head);
		/* Make sure another thread does not steal it */
		kref_get(&ctx->kref_used);
		VVIDEO_ASSERT(ctx->dev, vvideo_kref_read(&ctx->kref_used) >= 2);
		list_del_init(&ctx->kref_put.list_head);
		/* Add 1 for above kref_get() */
		counter = ctx->kref_put.counter + 1;
		ctx->kref_put.counter = 0;
		spin_unlock_irqrestore(&be->ctx_put.spinlock, spinlock_flags);
		while (counter-- > 0)
			vvideo_ctx_release(ctx, false);
		spin_lock_irqsave(&be->ctx_put.spinlock, spinlock_flags);
	}
	spin_unlock_irqrestore(&be->ctx_put.spinlock, spinlock_flags);
}

static void vvideo_be_kref_running_threads_release(struct kref *kref);

static int vvideo_thread(void *data)
{
	struct vvideo_thread_t *t = data;
	struct vvideo_be_t *be = t->be;

	snprintf(current->comm, TASK_COMM_LEN, VVIDEO_DRIVER ".%s",
		 t->name);
	allow_signal(SIGTERM);

	mutex_lock(&t->lock);
	/* Thread could start before creator set t->pid */
	t->pid = current->pid;
	t->struct_pid = get_pid(task_pid(current));

	/* A thread starts as part of be->idle_threads */
	while (!t->aborted) {
		int ret;

		mutex_unlock(&t->lock);
		VVIDEO_LOG_VERBOSE("%s, waiting for request\n", t->name);
		ret = wait_event_interruptible(be->wait,
					vvideo_req_received(t));
		if (ret) {
			VVIDEO_LOG("%s, interrupted (%d %d)\n",
				t->name, ret, signal_pending(current));
			flush_signals(current);
			/* We need to process possible VVIDEO2_REQ_RELEASE */
		}
		mutex_lock(&t->lock);
		if (t->aborted) {
			VVIDEO_LOG("%s, aborted\n", t->name);
			break;
		}
		/*
		 * Set t->working before doing much else and especially
		 * trying to create another thread,
		 * as this takes be->lock and might deadlock with another
		 * thread closing the last context, taking be->lock,
		 * executing vvideo_be_kref_open_ctxs_release() and
		 * synchronously killing all threads, including us.
		 * If the killer notices a t->working set thread, it will
		 * abandon operations, because this thread will probably
		 * open another context.
		 */
		t->working = true;
		mutex_unlock(&t->lock);

		if (be->ctx_put.notify) {
			be->ctx_put.notify = false;
			vvideo_be_ctx_put(be);
		}
		if (be->sysconf_notify) {
			be->sysconf_notify = false;
			VVIDEO_LOG("%s, sysconf\n", t->name);
			/* Call thread-based VMQ handlers */
			vmq_links_sysconf(be->links);
		}
		if (be->receive_notify) {
			be->receive_notify = false;
			VVIDEO_LOG_VERBOSE("%s, be->receive_notify\n", t->name);
			vmq_links_iterate(be->links, vvideo_link_receive_notify,
					  t);
			VVIDEO_LOG_VERBOSE("%s, be->receive_notify %d\n",
				t->name, be->receive_notify);
		}
		/* Returns old value - 1, even if not decremented */
		while (atomic_dec_if_positive(&be->ctx_close_notify) >= 0) {
			if (kref_put_mutex(&be->open_ctxs,
			    vvideo_be_kref_open_ctxs_release, &be->lock))
				mutex_unlock(&be->lock);
		}
		mutex_lock(&t->lock);
		t->working = false;
	}
	/* Do not clear t->aborted, this will be done by killer */
	mutex_unlock(&t->lock);

	/* Nothing to do when going to zero, so no need for kref_put_mutex */
	kref_put(&be->idle_threads, vvideo_be_kref_idle_threads_dummy_release);
	/*
	 * The DMABUF file handles only exist in vvideo_thread file table,
	 * so they must be cleaned from there, when last vvideo_thread ends.
	 */
	if (kref_put_mutex(&be->running_threads,
	    vvideo_be_kref_running_threads_release, &be->lock))
		mutex_unlock(&be->lock);
	VVIDEO_ASSERT_BE(vvideo_kref_read(&be->running_threads) >=
			 vvideo_kref_read(&be->idle_threads));

	mutex_lock(&t->lock);
	put_pid(t->struct_pid);
	t->struct_pid = NULL;
	/* Do not clear t->pid, this will be done by killer to free t_slot */
	mutex_unlock(&t->lock);

	VVIDEO_LOG_VERBOSE("%s EXITING, running/idle %d/%d\n", t->name,
		   (int)vvideo_kref_read(&be->running_threads),
		   (int)vvideo_kref_read(&be->idle_threads));
	complete_and_exit(&t->completion, 0);
	return 0;
}

/*----- VMQ callbacks -----*/

/* Only called from vvideo_thread() */

static void vvideo_cb_link_on(vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);

	VVIDEO_LOG("link %s\n", vl->name);
	vl->connected = true;
}

/* Only called from vvideo_thread() */

static VVIDEO_NOINLINE void vvideo_cb_link_off(vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_be_t *be = vl->be;
	struct vvideo_thread_t *t;
	int t_slot;
	struct vvideo_dev_t *vvideo_dev;
	int vvideo_dev_slot;

	VVIDEO_LOG("link %s\n", vl->name);
	vl->connected = false;

	/* Unblock all threads working on this link's devices */
	VVIDEO_FOR_ALL_THREADS(be, t_slot, t)
	{
		mutex_lock(&t->lock);
		/* link and device change for every request */
		if (t->m && t->m->dev && t->m->dev->link == vl) {
			VVIDEO_LOG("link %s Sending SIGTERM to %s pid %d\n",
				vl->name, t->name, t->pid);
			kill_pid(t->struct_pid, SIGTERM, 1);
		}
		mutex_unlock(&t->lock);
	}
	/* Set reset_pending for all link's devices */
	VVIDEO_FOR_ALL_DEVS(vl, vvideo_dev_slot, vvideo_dev)
	{
		mutex_lock(&vvideo_dev->lock);
		vvideo_kref_get_unlocked(&vvideo_dev->kref);
		vvideo_dev->reset_pending = true;
		mutex_unlock(&vvideo_dev->lock);
		vvideo_dev_release(vvideo_dev);
	}
}

/* Only called from vvideo_thread() */

static void vvideo_cb_link_off_completed(vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);

	(void)vl;
	VVIDEO_LOG("link %s\n", vl->name);
}

/* Interrupt-level callback */

static void vvideo_cb_sysconf_notify(vmq_links_t *links)
{
	struct vvideo_be_t *be = VVIDEO_LINKS_BE(links);

	VVIDEO_LOG("entered\n");
	/*
	 * First thread to wake up will process sysconf
	 * for all links and clear be->sysconf_notify.
	 */
	be->sysconf_notify = true;
	wake_up(&be->wait);
}

/* Interrupt-level callback */

static void vvideo_cb_receive_notify(vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_be_t *be = vl->be;
	/*
	 * We will not be informed for every msg on link,
	 * just about the first one, so we have to empty
	 * all of them before going back to sleep.
	 */
	be->receive_notify = true;
	vl->receive_notify = true;
	VVIDEO_LOG_VERBOSE("link %s be receive_notify %d vl %d\n", vl->name,
			   be->receive_notify, vl->receive_notify);
	wake_up(&be->wait);
}

/* Interrupt-level callback */

static void vvideo_cb_return_notify(vmq_link_t *link2)
{
	void *msg;	/* struct VVideo2Request* */

	while (!vmq_return_msg_receive(link2, &msg))
		vmq_return_msg_free(link2, msg);
	vvideo_link_send_events(VVIDEO_LINK(link2));
}

static const vmq_callbacks_t vvideo_callbacks = {
	.link_on = vvideo_cb_link_on,
	.link_off = vvideo_cb_link_off,
	.link_off_completed = vvideo_cb_link_off_completed,
	.sysconf_notify = vvideo_cb_sysconf_notify,
	.receive_notify = vvideo_cb_receive_notify,
	.return_notify = vvideo_cb_return_notify,
	.get_tx_config = NULL,
	.get_rx_config = vvideo_cb_get_xx_config
};

static const vmq_xx_config_t vvideo_tx_config = {
	.msg_count = 4,
	.msg_max = sizeof(struct VVideo2Request),
	.pdev_level = VMQ_PDEV_NONE
};

/*----- Initialization and cleanup code -----*/

/*
 * Called from vvideo_link_free() only.
 * Must be called from a vvideo_thread,
 * because it operates on file descriptors.
 */

static VVIDEO_NOINLINE void vvideo_dev_cleanup(struct vvideo_dev_t *vvideo_dev)
{
	VVIDEO_LOG("device %s releasing remaining open contexts\n",
		   vvideo_dev->name);
	vvideo_dev_release_ctxs(vvideo_dev);
	VVIDEO_LOG("device %s done\n", vvideo_dev->name);
}

/*
 * Called from vvideo_link_init() during driver initialization phase.
 */
static void vvideo_dev_init(struct vvideo_dev_t *vvideo_dev, unsigned int minor,
			    struct vvideo_link_t *vl)
{
	struct vvideo_ctx_t *ctx;
	int ctx_slot;

	vvideo_dev->link = vl;
	vvideo_dev->minor = minor;
	snprintf(vvideo_dev->name, sizeof(vvideo_dev->name), "%d.%d",
		 vvideo_dev->link->number, minor);
	mutex_init(&vvideo_dev->lock);
	/* vvideo_dev->reset_pending starts at zero */
	/* vvideo_dev->kref starts at zero */
	/* vvideo_dev->last_be_context starts at zero */
	/* vvideo_dev->errors start at zero */
	/* vvideo_dev->warnings start at zero */
	/* vvideo_dev->stats start at zero */
	VVIDEO_FOR_ALL_CTXS(vvideo_dev, ctx_slot, ctx)
	{
		vvideo_ctx_init(ctx, vvideo_dev);
	}
	VVIDEO_INFO("device %s (%s) created for link %s\n", vvideo_dev->name,
		    vvideo_dev_config(vvideo_dev)->name, vl->name);
}

/*
 * Executed as short-lived kthread from vvideo_module_init()
 * to have a clean environment to start request threads which
 * must share file descriptors.
 * We could potentially have separate file descriptor spaces
 * for each device or context, but this makes observation
 * more difficult, as same handles would be used for different files.
 */

static int vvideo_be_start_threads(void *data)
{
	struct vvideo_be_t *be = data;
	struct vvideo_thread_t *t;
	int t_slot;
	/*
	 * Obtain private current->files, not shared with all other
	 * kernel threads, which can be extended beyond 64
	 * entries (does not work with kthread_run() on certain
	 * kernels) and shared with new threads.
	 */
	VVIDEO_LOG("Initial current->files->count %d\n",
		   atomic_read(&current->files->count));
	be->sys_unshare(CLONE_FILES);
	VVIDEO_LOG("Final current->files->count %d\n",
		   atomic_read(&current->files->count));

	/* Make sure 0 is not a valid DMABUF file descriptor */
	be->fd_zero = get_unused_fd_flags(0);

	/* Initialize request thread context */
	VVIDEO_FOR_ALL_THREADS(be, t_slot, t)
	{
		t->be = be;
		snprintf(t->name, sizeof(t->name), "th%d", t_slot);
		/* dev inited dynamically */
		/* req inited dynamically */
		/* aborted is zero */
		init_completion(&t->completion);
		/* pid is zero */
		/* struct_pid is NULL */
		mutex_init(&t->lock);
		/* working is zero */
	}
	/* Start first thread */
	mutex_lock(&be->lock);
	vvideo_be_start_next_thread(be);
	mutex_unlock(&be->lock);
	return 0;
}

/*
 * Called first time from vvideo_be_kref_running_threads_release().
 * Must be called from a vvideo_thread if there are open files,
 * because it operates on file descriptors.
 * Called second time from vvideo_module_cleanup(),
 * in case there were no threads, so it must be idempotent.
 */

static _Bool vvideo_link_free(vmq_link_t *link2, void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);

	VVIDEO_LOG("link %d vl %p\n", vmq_peer_osid(link2), vl);
	if (!vl)
		return false; /* Second call */
	if (vl->devs) {
		struct vvideo_dev_t *vvideo_dev;
		int vvideo_dev_slot;

		VVIDEO_FOR_ALL_DEVS(vl, vvideo_dev_slot, vvideo_dev)
		{
			vvideo_dev_cleanup(vvideo_dev);
		}
		kfree(vl->devs);
		vl->devs = NULL;
	}
	kfree(vl->configs.configs);
	vl->configs.configs = NULL;
	kfree(vl->msgs);
	vl->msgs = NULL;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	/* If vfence2_connect() fails, we come here with NULL pointers */
	if (vl->vfence2_connection) {
		(void)vfence2_disconnect(vl->vfence2_connection);
		vl->vfence2_connection = 0;
	}
#endif
	kfree(vl);
	VVIDEO_LINK(link2) = NULL; /* Make second call return immediately */
	return false; /* Keep iterating */
}

/* When the last running thread terminates */

static VVIDEO_NOINLINE void vvideo_be_kref_running_threads_release(
							struct kref *kref)
{
	struct vvideo_be_t *be =
		container_of(kref, struct vvideo_be_t, running_threads);

	VVIDEO_LOG("entered\n");
	vmq_links_iterate(be->links, vvideo_link_free, NULL);
	if (be->fd_zero >= 0) {
		put_unused_fd(be->fd_zero);
		be->fd_zero = -1;
	}
	VVIDEO_LOG("exit\n");
}

/* Only called from vvideo_module_init() */

static _Bool vvideo_link_init(vmq_link_t *link2, void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_be_t *be = cookie;
	struct vvideo_dev_t *vvideo_dev;
	int vvideo_dev_slot;

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	const int ret =
		vfence2_connect(be->vfence2_client, vmq_peer_osid(link2),
				&vl->vfence2_connection);
	if (ret) {
		VVIDEO_ERR("vfence2_connect(%d) failed (%d)\n",
			   vmq_peer_osid(link2), ret);
		return true; /* Stop iterating */
	}
#endif
	/* vl alloced and partially initialized by vvideo_cb_get_xx_config() */
	/* Not yet vl->connected, wait for "link_on" */
	vl->link = link2;
	vl->number = be->link_count++;
	snprintf(vl->name, sizeof(vl->name), "%d.%d", vl->number,
		 vmq_peer_osid(link2));
	vl->be = be;
	/* vl->configs was initialized by vvideo_cb_get_xx_config() */
	/* vl->xx_config ditto */
	/* vl->receive_notify starts at zero */
	if (vl->configs.count <= 0) {
		VVIDEO_ERR("link %s No devices exported\n", vl->name);
		return true; /* Stop iterating */
	}
	VVIDEO_LOG("link %s count %d\n", vl->name, vl->configs.count);
	vl->devs = kcalloc(vl->configs.count, sizeof(struct vvideo_dev_t),
			   GFP_KERNEL);
	if (!vl->devs) {
		VVIDEO_ERR("link %s Out of memory\n", vl->name);
		return true; /* Stop iterating */
	}
	vl->dev_width = __fls(vl->configs.count) + 1;
	if (vl->dev_width >= 31) {
		VVIDEO_ERR("link %s Too many devices exported\n", vl->name);
		return true; /* Stop iterating */
	}
	/* Reserve a full hex digit/number for minor */
	vl->dev_width = vvideo_round_hex(vl->dev_width);
	vl->dev_mask = (1 << vl->dev_width) - 1;
	VVIDEO_LOG("link %s width %d mask %#x\n", vl->name, vl->dev_width,
		   vl->dev_mask);
	vl->msgs = kcalloc(vl->xx_config.msg_count, sizeof(struct vvideo_msg_t),
			   GFP_KERNEL);
	if (!vl->msgs) {
		VVIDEO_ERR("link %s Out of memory for msgs\n", vl->name);
		return true; /* Stop iterating */
	}
	mutex_init(&vl->lock);
	spin_lock_init(&vl->events.spinlock);
	INIT_LIST_HEAD(&vl->events.list_head);
	be->max_reqs += vl->xx_config.msg_count;
	VVIDEO_FOR_ALL_DEVS(vl, vvideo_dev_slot, vvideo_dev)
	{
		vvideo_dev_init(vvideo_dev, vvideo_dev_slot, vl);
	}
	return false; /* Keep iterating */
}

/*----- /proc/nk/vvideo2-be -----*/

#ifdef CONFIG_PROC_FS

/* vvideo_thread_t::lock must be Unlocked */

static const char *vvideo_thread_req_data(struct vvideo_thread_t *t,
					  enum VVideo2ReqNum *req_num,
					  int *result, unsigned int *be_context,
					  char *dev_name,
					  size_t dev_name_len, int *slot,
					  unsigned int *transaction)
{
	const struct vvideo_dev_t *dev;
	const struct VVideo2Request *req;
	const char *req_name;

	mutex_lock(&t->lock);
	dev = t->m ? t->m->dev : NULL;
	req = t->m ? t->m->req : NULL;
	snprintf(dev_name, dev_name_len, "%s",
		 dev ? vvideo_dev_config(dev)->name : "None");
	if (req) {
		*req_num = req->req_num;
		*result = req->result;
		*be_context = req->be_context;
		req_name = vvideo_req_full(req);
		if (dev)
			*slot = vmq_msg_rx_slot(dev->link->link, req);
		else
			*slot = -1;
		*transaction = req->transaction;
	} else {
		*req_num = VVIDEO2_REQ_NONE;
		*result = 0;
		*be_context = 0;
		req_name = "None";
		*slot = -1;
		*transaction = 0;
	}
	mutex_unlock(&t->lock);
	return req_name;
}

struct vvideo_proc_t {
	struct seq_file *seq;
	_Bool ext;
};

static void vvideo_ctx_proc_show(struct vvideo_ctx_t *ctx,
				 struct vvideo_proc_t *p)
{
	struct seq_file *seq = p->seq;
	_Bool need_newline = false;
	int dmabufs_set = 0;
	int dmabufs_valid = 0;
	struct vvideo_poll_t *poll;
	int polls = 0;
	unsigned long spinlock_flags;
	enum v4l2_buf_type buf_type;
	char buf[max(VVIDEO_DMABUF_FORMAT, VVIDEO_CTX_FILE_FORMAT_MAX)];
	const struct vvideo_dmabuf_t *d;
	int d_slot;

	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		if (vvideo_dmabuf_is_set(d))
			++dmabufs_set;
		if (d->valid)
			++dmabufs_valid;
	}
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	list_for_each_entry(poll, &ctx->poll.list_head, ctx_list)
		++polls;
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
	seq_printf(seq,
	" File %s ctx %s kref %d/%d %s polls %d dmabufs %d/%d sop %#x aq %c\n",
		   vvideo_ctx_file_format(ctx, buf, sizeof(buf)), ctx->name,
		   vvideo_kref_read(&ctx->kref_busy),
		   vvideo_kref_read(&ctx->kref_used),
		   vvideo_ctx_state_name[ctx->state], polls,
		   dmabufs_valid, dmabufs_set,
		   ctx->streamoff_pending, ctx->async_queue ? 'Y' : 'N');
	for (buf_type = 0; buf_type < VVIDEO_BUF_TYPE_MAX; ++buf_type) {
		if (ctx->streamon[buf_type] < 0)
			continue;
		seq_printf(seq, " %s=%s", vvideo_v4l2_buf_type_short(buf_type),
			ctx->streamon[buf_type] ? "Y" : "N");
		need_newline = 1;
	}
	if (need_newline)
		seq_puts(seq, "\n");
	if (!p->ext)
		return;
	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		if (!vvideo_dmabuf_is_set(d))
			continue;
		seq_printf(seq, " %2d %2d %s\n", (int) (ctx - ctx->dev->ctx),
			d_slot, vvideo_dmabuf_format(d, buf, sizeof(buf)));
	}
}

static void vvideo_dev_proc_show(struct vvideo_dev_t *vvideo_dev,
				 struct vvideo_proc_t *p)
{
	struct vvideo_link_t *vl = vvideo_dev->link;
	struct seq_file *seq = p->seq;
	const struct vdev_errors *e = &vvideo_dev->errors;
	const _Bool errors = vvideo_non_zero(&vvideo_dev->errors,
					sizeof(vvideo_dev->errors));
	const struct vdev_warnings *w = &vvideo_dev->warnings;
	const _Bool warnings = vvideo_non_zero(&vvideo_dev->warnings,
					sizeof(vvideo_dev->warnings));
	const struct vdev_stats *s = &vvideo_dev->stats;
	const _Bool stats = vvideo_non_zero(&vvideo_dev->stats,
					sizeof(vvideo_dev->stats));
	struct vvideo_ctx_t *ctx;
	int ctx_slot;

	seq_printf(seq, "%-4s %2d %c%c %2d %s\n", vvideo_dev->name,
		   vmq_peer_osid(vl->link),
		   vvideo_dev->reset_pending ? 'R' : '.',
		   vl->connected ? 'C' : '.',
		   (int)vvideo_kref_read(&vvideo_dev->kref),
		   vvideo_dev_config(vvideo_dev)->name);

	VVIDEO_FOR_ALL_CTXS(vvideo_dev, ctx_slot, ctx)
	{
		if (!ctx->be_context && ctx->state == VVIDEO_CS_FREE)
			continue;
		vvideo_ctx_proc_show(ctx, p);
	}
	if (errors)
		seq_puts(seq, " Errors:");
#define X(x)								       \
do {								       \
	if (e->x)						       \
		seq_printf(seq, " " #x ":%d", e->x);		       \
} while (0)

	X(gid_invalid);
	X(out_of_slots);
	X(dmabuf_import);
	X(dma_buf_fd);
	X(dma_buf_get);
	X(dmabuf_export);
	X(fd_negative);
	X(map_fd_back);
	X(not_dmabuf);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	X(import_fence);
#endif
	X(b_length);
	X(open_ebusy);
	X(no_private_data);
	X(req_unknown);
	X(too_many_contexts);
	X(could_not_open);
	X(could_not_read);
	X(name_mismatch);
	X(find_node);
#ifdef VVIDEO_USE_FD
	X(sys_open);
#else
	X(filp_open);
#endif
	X(invalid_context);
	X(release_invalid);
	X(poll_error);
	X(edid_blocks);
	X(p_count);
	X(ext_gid);
#ifdef VVIDEO_DPBS
	X(dpbs.multiple);
	X(dpbs.shared_get);
	X(dpbs.vmap);
#endif
#undef X
	if (errors)
		seq_puts(seq, "\n");

	if (warnings)
		seq_puts(seq, " Warnings:");
#define X(x)                                                                   \
do {                                                                   \
	if (w->x)                                                      \
		seq_printf(seq, " " #x ":%d", w->x);		       \
} while (0)

	X(f_count);
	X(sys_close);
#ifndef VVIDEO_USE_FD
	X(filp_close);
#endif
	X(multiple_release_fd);
#ifdef VVIDEO_ASSERTS
	X(failed_assert);
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	X(export_fence);
#endif
	X(poll.cookie);
	X(poll.dup_wait);
	X(poll.f_op);
	X(poll.max);
	X(poll.too_many);
	X(kmem_cache_alloc);
#ifdef VVIDEO_DQBUF
	X(dup_qbuf);
	X(dup_dqbuf);
#endif
#ifdef VVIDEO_DPBS
	X(dpbs.index);
	X(dpbs.size);
#endif
#undef X
	if (warnings)
		seq_puts(seq, "\n");

	if (stats)
		seq_puts(seq, " Stats:");
#define X(x)                                                                   \
do {                                                                   \
	if (s->x)                                                      \
		seq_printf(seq, " " #x ":%d", s->x);	               \
} while (0)

	X(fc_down);
	X(ctx_wrap);
	X(ctx_clash);
	X(deferred_returns);
	X(behind_async);
	X(wait_async);
	X(wait_signal);
	X(next_none);
	X(next_async);
	X(next_sync);
	X(gid2fd_0);
	X(gid2fd_n);
	X(gid2fd_alloc);
	X(poll.discard);
	X(poll.dup);
	X(poll.eagain);
	X(poll.event);
	X(poll.key_null);
	X(poll.key_set);
	X(poll.mask);
	X(poll.no_event);
	X(poll.no_list);
	X(poll.no_wait);
	X(poll.second);
	X(poll.wait);
	X(poll.wakeup);
	X(fences_ok);
	X(fences_removed);
	X(stale_abort);
	X(stale_ctx);
	X(release_msg);
	X(release_sent);
	X(flag_error);
	X(deleted_fd);
#ifdef VVIDEO_DPBS
	X(dpbs.buffer);
	X(dpbs.dqbuf);
	X(dpbs.export);
	X(dpbs.plane);
	X(dpbs.repeat);
	X(dpbs.s_ctrl);
	X(dpbs.zero);
#endif
#undef X
	if (stats)
		seq_puts(seq, "\n");
}

static _Bool vvideo_link_proc_show(vmq_link_t *link2, void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_proc_t *p = cookie;
	struct seq_file *seq = p->seq;
	struct vvideo_dev_t *vvideo_dev;
	int vvideo_dev_slot;

	if (!vl)
		return false; /* We are dismantling /proc */
	seq_printf(seq,
	" configs %d dev_width/mask %d/%#x msgs %d connected %d receive %d\n",
		   vl->configs.count, vl->dev_width, vl->dev_mask,
		   vl->xx_config.msg_count, vl->connected, vl->receive_notify);
#ifdef VVIDEO_REMEMBER_TRANSACTIONS
	seq_printf(seq, "next_transaction %d:",
		vl->next_transaction % VVIDEO_LAST_TRANSACTIONS);
	for (vvideo_dev_slot = 0; vvideo_dev_slot < VVIDEO_LAST_TRANSACTIONS;
	     ++vvideo_dev_slot)
		seq_printf(seq, " %d", vl->transactions[vvideo_dev_slot]);
	seq_puts(seq, "\n");
#endif
	VVIDEO_FOR_ALL_DEVS(vl, vvideo_dev_slot, vvideo_dev)
	{
		vvideo_dev_proc_show(vvideo_dev, p);
	}
	return false;
}

static int vvideo_seq_proc_show(struct seq_file *seq, void *v)
{
	/* For seq_file, file->private_data points to struct seq_file */
	struct vvideo_be_t **bep = seq->private;
	struct vvideo_be_t *be = *bep;
	struct vvideo_proc_t proc;
	struct vvideo_thread_t *t;
	int t_slot;

	(void)v;
	proc.seq = seq;
	proc.ext = (void *)bep != (void *)be;
	seq_puts(seq, "vVideo v2 backend (revision 2021-06-04)\n");
	seq_puts(seq,
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		" VF"
#else
		" !VF"
#endif
#ifdef VVIDEO_DEBUG
		" DE"
#else
		" !DE"
#endif
#ifdef VVIDEO_COMMS
		" CO"
#else
		" !CO"
#endif
#ifdef VVIDEO_VERBOSE
		" VE"
#else
		" !VE"
#endif
#ifdef VVIDEO_MSLEEP_CLOSE
		" MC"
#else
		" !MC"
#endif
#ifdef VVIDEO_USE_FD
		" FD"
#else
		" !FD"
#endif
#ifdef VVIDEO_DMABUF
		" DM"
#else
		" !DM"
#endif
#ifdef VVIDEO_ASSERTS
		" AS"
#else
		" !AS"
#endif
#ifdef VVIDEO_REMEMBER_TRANSACTIONS
		" RT"
#else
		" !RT"
#endif
#ifdef VVIDEO_DQBUF
		" DQ"
#else
		" !DQ"
#endif
#ifdef VVIDEO_POLL_WQUEUES
		" WQ"
#else
		" !WQ"
#endif
#ifdef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
		" DV"
#else
		" !DV"
#endif
#ifdef VVIDEO_LATE_PROC_EXIT
		" LPE"
#else
		" !LPE"
#endif
#ifdef VVIDEO_LOCAL_GID
		" LG"
#else
		" VDMAHEAP"
#endif
#ifdef VVIDEO_DPBS
		" DPBS"
#else
		" !DPBS"
#endif
		" ");
	seq_printf(seq, "(%d %d %d %d %d %d %d %d)\n",
		VVIDEO_DMABUF_MAX,
		VVIDEO_BUF_TYPE_MAX,
		VVIDEO_CTX_MAX,
		VVIDEO_CTX_POLL_MAX,
		VVIDEO_ERR_MAX,
		VVIDEO_WARN_MAX,
#ifdef VVIDEO_POLL_WQUEUES
		-1,
#else
		VVIDEO_POLL_MAX,
#endif
#ifdef VVIDEO_LOCAL_GID
		0
#else
		VVIDEO_VDMAHEAP_GRACE_DELAY_MS
#endif
	);
	seq_printf(seq,
	    "links %d sysconf %d receive %d max_reqs %u threads/idle %u/%d\n",
		be->link_count, be->sysconf_notify, be->receive_notify,
		be->max_reqs, (int)vvideo_kref_read(&be->running_threads),
		(int)vvideo_kref_read(&be->idle_threads));

	seq_puts(seq, " Stats:");
#define X(x)                                                                   \
	do {                                                                   \
		if (be->stats.x)                                               \
			seq_printf(seq, " " #x ":%d", be->stats.x);            \
	} while (0)

	X(thread_starts);
	X(kills);
	X(stopped_kills);
	X(max_threads);
	X(kernel_thread);
	X(max_running);
	X(invalid_dev);
	X(sig_processed);
	X(sig_delivered);
	X(kill_retry);
#undef X
	seq_puts(seq, "\n");
#ifdef VVIDEO_ASSERTS
	if (be->warnings.failed_assert)
		seq_printf(seq, " Warnings: failed_assert:%d\n",
			   be->warnings.failed_assert);
#endif

	seq_puts(seq, "L.Mi Pr RC RC Config\n");
	if (proc.ext)
		seq_printf(seq, " Ct Sl %s\n", vvideo_dmabuf_legend);
	if (be->links)
		vmq_links_iterate(be->links, vvideo_link_proc_show, &proc);

	seq_puts(seq,
	"Sl MS R Transacti Result-- Ctx--- AWD PID-- Name Request- Device\n");
	if (!be->threads)
		return 0;
	VVIDEO_FOR_ALL_THREADS(be, t_slot, t)
	{
		enum VVideo2ReqNum req_num;
		int result;
		unsigned int be_context;
		const char *req_name;
		char dev_name[64];
		int slot;
		unsigned int transaction;

		if (t->pid <= 0)
			continue;
		req_name = vvideo_thread_req_data(t, &req_num, &result,
						  &be_context, dev_name,
						  sizeof(dev_name), &slot,
						  &transaction);
		seq_printf(seq,
			   "%2d %2d %1d %9d %8x %6x %c%c%u %5d %4s %-8s %s\n",
			   t_slot, slot, req_num, transaction, result,
			   be_context, t->aborted ? 'A' : '.',
			   t->working ? 'W' : '.', t->deferred_pending, t->pid,
			   t->name, req_name, dev_name);
	}
	return 0;
}

static int vvideo_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, vvideo_seq_proc_show, PDE_DATA(inode));
}

static const struct proc_ops vvideo_proc_ops = {
	.proc_open = vvideo_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

#define VVIDEO_PROC_STD "nk/" VVIDEO_DRIVER
#define VVIDEO_PROC_EXT "nk/" VVIDEO_DRIVER ".ext"

static int __init vvideo_proc_init(struct vvideo_be_t *be)
{
	struct proc_dir_entry *ent;

	be->proc_normal = be->proc_ext = be;
	ent = proc_create_data(VVIDEO_PROC_STD, 0444, NULL, &vvideo_proc_ops,
			       &be->proc_normal);
	if (!ent) {
		VVIDEO_ERR("Could not create /proc/" VVIDEO_PROC_STD "\n");
		return -ENOMEM;
	}
	ent = proc_create_data(VVIDEO_PROC_EXT, 0444, NULL, &vvideo_proc_ops,
			       &be->proc_ext);
	if (!ent) {
		VVIDEO_ERR("Could not create /proc/" VVIDEO_PROC_EXT "\n");
		remove_proc_entry(VVIDEO_PROC_STD, NULL);
		return -ENOMEM;
	}
	be->proc_exists = true;
	return 0;
}

static void vvideo_proc_exit(struct vvideo_be_t *be)
{
	if (be->proc_exists) {
		remove_proc_entry(VVIDEO_PROC_STD, NULL);
		remove_proc_entry(VVIDEO_PROC_EXT, NULL);
		be->proc_exists = false;
	}
}
#else
#define vvideo_proc_init(x)
#define vvideo_proc_exit(x)
#endif

/*----- Entry and exit -----*/

/*
 * vvideo_module_cleanup() function is called
 * to free driver resources. It is used during driver
 * initialization phase (if something goes wrong) and
 * during driver exit phase.
 * It must stop threads still running inside driver.
 */
static void vvideo_module_cleanup(struct vvideo_be_t *be)
{
	struct vvideo_thread_t *t;
	int t_slot;

	VVIDEO_LOG("entered\n");
#ifndef VVIDEO_LATE_PROC_EXIT
	vvideo_proc_exit(be);
#endif
	if (be->links)
		/*
		 * Detach all interrupt handlers, waiting for handler
		 * termination if necessary.
		 */
		vmq_links_stop(be->links);
	if (be->threads)
		/*
		 * Make sure threads do not accept new requests,
		 * which could be sent by frontend when it gets
		 * error replies from blocking calls.
		 */
		VVIDEO_FOR_ALL_THREADS(be, t_slot, t)
		{
			mutex_lock(&t->lock);
			t->aborted = true;
			mutex_unlock(&t->lock);
		}
	if (be->links) {
		/* This acts on transmission side only, not reception */
		vmq_links_abort(be->links);
		/* Tell peer we will not process new invocations */
		vmq_links_off(be->links);
	}
	if (be->threads) {
		while (1) {
			/*
			 * Wait for threads to terminate.
			 * Last terminating one will clean be->links.
			 */
			VVIDEO_FOR_ALL_THREADS(be, t_slot, t)
			{
				pid_t pid;

				mutex_lock(&be->lock);
				if (t->pid <= 0) {
					mutex_unlock(&be->lock);
					continue;
				}
				pid = t->pid;
				/*
				 * Do not kill again anymore and do not
				 * attempt to
				 * wait_for_completion(), which can only
				 * be used once
				 * per complete_and_exit().
				 * We must mark threads this way, so that
				 * on last context close,
				 * vvideo_be_kref_open_ctxs_release()
				 * does not attempt to kill and join this
				 * thread again.
				 * Note that proc file has been destroyed,
				 * so we cannot see that PID has been
				 * cleared for this thread.
				 */
				t->pid = 0;
				mutex_unlock(&be->lock);
				wake_up_all(&be->wait);

				/* Wake it up even if blocked in V4L2 call */
				mutex_lock(&t->lock);
				if (t->working) {
					(void)pid;
					VVIDEO_LOG(
					"Sending SIGTERM to %s pid %d\n",
						t->name, pid);
					kill_pid(t->struct_pid, SIGTERM, 1);
				}
				mutex_unlock(&t->lock);

				wait_for_completion(&t->completion);
				VVIDEO_LOG("thread %d completed\n", t_slot);
			}
			/*
			 * We can have threads which have been created, but
			 * t->pid has not yet been set, only the kref has
			 * been incremented. Await them.
			 */
			if (vvideo_kref_read(&be->running_threads) <= 0)
				break;
			++be->stats.kill_retry;
		}
	}
	VVIDEO_LOG("kill_retry %d\n", be->stats.kill_retry);
	kfree(be->threads);
	be->threads = NULL;

	if (be->links) {
		/* If there were no threads, we still need to clean */
		vmq_links_iterate(be->links, vvideo_link_free, NULL);
		vmq_links_finish(be->links);
		be->links = NULL;
	}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (be->vfence2_client) {
		const int ret = vfence2_destroy_client(be->vfence2_client);

		if (ret)
			VVIDEO_WARN("vfence2_destroy_client() failed (%d)\n",
				    ret);
		be->vfence2_client = NULL;
	}
#endif
#ifndef VVIDEO_LOCAL_GID
	if (be->vdmaheap_client) {
		const int ret = vdmaheap_kapi_client_release(be->vdmaheap_client);

		if (ret)
			VVIDEO_WARN("vdmaheap_kapi_client_release() failed (%d)\n",
				    ret);
		be->vdmaheap_client = NULL;
	}
#endif
	kmem_cache_destroy(be->poll_kmem_cache);
	be->poll_kmem_cache = NULL;
#ifdef VVIDEO_LATE_PROC_EXIT
	vvideo_proc_exit(be);
#endif
	VVIDEO_LOG("DONE\n");
}

static struct vvideo_be_t vvideo_be;

/*
 * vvideo_module_init() is the 1st function
 * called by linux driver framework. It initializes
 * our driver.
 *
 * It finds how many devices will be managed by this driver.
 *
 * It allocates memory for all devices descriptors.
 *
 * It finds all the devices managed by this driver
 * and "assigns" them minors.
 */
static int vvideo_module_init(void)
{
	struct vvideo_be_t *be = &vvideo_be;
#ifndef VVIDEO_LOCAL_GID
	struct vdmaheap_client_props props;
#endif
	int ret = 0;
	vlx_thread_t thread;

	be->name = "be"; /* For warnings */
#ifdef MODULE
#if KERNEL_VERSION(4, 17, 0) <= LINUX_VERSION_CODE
	be->sys_unshare = (void *)kallsyms_lookup_name("ksys_unshare");
#else
	be->sys_unshare = (void *)kallsyms_lookup_name("sys_unshare");
#endif
	if (!be->sys_unshare) {
		VVIDEO_ERR("Could not find symbol 'ksys_unshare'\n");
		return -EINVAL;
	}
	be->kernel_thread = (void *)kallsyms_lookup_name("kernel_thread");
	if (!be->kernel_thread) {
		VVIDEO_ERR("Could not find symbol 'kernel_thread'\n");
		return -EINVAL;
	}
#ifdef VVIDEO_USE_FD
#if KERNEL_VERSION(4, 17, 0) <= LINUX_VERSION_CODE
	be->sys_open = (void *)kallsyms_lookup_name("ksys_open");
	if (!be->sys_open) {
		VVIDEO_ERR("Could not find symbol 'ksys_open'\n");
		return -EINVAL;
	}
	be->sys_ioctl = (void *)kallsyms_lookup_name("ksys_ioctl");
	if (!be->sys_ioctl) {
		VVIDEO_ERR("Could not find symbol 'ksys_ioctl'\n");
		return -EINVAL;
	}
	be->sys_read = (void *)kallsyms_lookup_name("ksys_read");
	if (!be->sys_read) {
		VVIDEO_ERR("Could not find symbol 'ksys_read'\n");
		return -EINVAL;
	}
#else
	be->sys_open = (void *)kallsyms_lookup_name("sys_open");
	if (!be->sys_open) {
		VVIDEO_ERR("Could not find symbol 'sys_open'\n");
		return -EINVAL;
	}
	be->sys_ioctl = (void *)kallsyms_lookup_name("sys_ioctl");
	if (!be->sys_ioctl) {
		VVIDEO_ERR("Could not find symbol 'sys_ioctl'\n");
		return -EINVAL;
	}
	be->sys_read = (void *)kallsyms_lookup_name("sys_read");
	if (!be->sys_read) {
		VVIDEO_ERR("Could not find symbol 'sys_read'\n");
		return -EINVAL;
	}
#endif
#endif
#else
#if KERNEL_VERSION(4, 17, 0) <= LINUX_VERSION_CODE
	be->sys_unshare = ksys_unshare;
#else
	be->sys_unshare = sys_unshare;
#endif
	be->kernel_thread = kernel_thread;
#ifdef VVIDEO_USE_FD
#if KERNEL_VERSION(4, 17, 0) <= LINUX_VERSION_CODE
	be->sys_open = ksys_open;
	be->sys_ioctl = ksys_ioctl;
	be->sys_read = ksys_read;
#else
	be->sys_open = sys_open;
	be->sys_ioctl = sys_ioctl;
	be->sys_read = sys_read;
#endif
#endif
#endif

	be->fd_zero = -1;
	mutex_init(&be->lock);
	init_waitqueue_head(&be->wait);
	be->ctx_put.notify = false;
	spin_lock_init(&be->ctx_put.spinlock);
	INIT_LIST_HEAD(&be->ctx_put.list_head);

	be->poll_kmem_cache = KMEM_CACHE(vvideo_poll_t, 0);
	if (!be->poll_kmem_cache) {
		VVIDEO_ERR("could not create kmem_cache\n");
		ret = -ENOMEM;
		vvideo_module_cleanup(be);
		goto out;
	}
#ifndef VVIDEO_LOCAL_GID
	ret = vdmaheap_kapi_client_create(&be->vdmaheap_client);
	if (ret) {
		VVIDEO_ERR("vdmaheap_kapi_client_create() failed (%d)\n", ret);
		vvideo_module_cleanup(be);
		goto out;
	}
	/*
	 * Relax vdmaheap strict import semantics. This is the first level
	 * of optimization. It is a prerequisite for enabling import cache.
	 */
	props.strict_import = false;
	/*
	 * Import cache enabling requested (hint). Whether the cache is
	 * actually enabled depends on vdmaheap configuration.
	 */
	props.grace.requested = msecs_to_jiffies(VVIDEO_VDMAHEAP_GRACE_DELAY_MS);
	if (!props.grace.requested)
		props.grace.requested = 1;
	props.grace.accepted = -1;
	ret = vdmaheap_kapi_set_client_props(be->vdmaheap_client, &props);
	if (ret) {
		VVIDEO_ERR("vdmaheap_kapi_set_client_props() failed (%d)\n", ret);
		goto out;
	}
	VVIDEO_INFO("vdmaheap cache enabled, grace delay %d ms\n",
		jiffies_to_msecs(props.grace.requested));
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	ret = vfence2_create_client(VVIDEO_DRIVER, &be->vfence2_client);
	if (ret) {
		VVIDEO_ERR("vfence2_create_client() failed (%d)\n", ret);
		vvideo_module_cleanup(be);
		goto out;
	}
#endif
	ret = vmq_links_init_ex(&be->links, VVIDEO2_VLINK_NAME,
				&vvideo_callbacks, &vvideo_tx_config,
				NULL /*rx_config*/, be, false /*!frontend*/);
	if (ret) {
		VVIDEO_ERR("Failed VMQ link initialization (%d)\n", ret);
		vvideo_module_cleanup(be);
		goto out;
	}
	if (vmq_links_iterate(be->links, vvideo_link_init, be)) {
		/* Error message issued */
		ret = -EINVAL;
		vvideo_module_cleanup(be);
		goto out;
	}
	VVIDEO_LOG("be->max_reqs %d\n", be->max_reqs);
	/* Passing size 0 to kzalloc() gives ZERO_SIZE_PTR instead of NULL */
	if (!be->max_reqs) {
		VVIDEO_ERR("No V4L2 devices to export\n");
		ret = -ENOENT;
		vvideo_module_cleanup(be);
		goto out;
	}
	be->threads = kcalloc(be->max_reqs, sizeof(struct vvideo_thread_t),
			      GFP_KERNEL);
	if (!be->threads) {
		VVIDEO_ERR("Out of memory for %d threads\n", be->max_reqs);
		ret = -ENOMEM;
		vvideo_module_cleanup(be);
		goto out;
	}
	/* Start initial request thread from a starter thread */
	ret = vlx_thread_start_ex(&thread, vvideo_be_start_threads, be,
				  "vvideo2-be-starter", 1 /*will_suicide*/);
	if (ret) {
		VVIDEO_ERR("Could not create starter thread (%d)\n", ret);
		vvideo_module_cleanup(be);
		goto out;
	}
	vlx_thread_join(&thread); /* void */
	if (!be->threads[0].pid) {
		VVIDEO_ERR("Could not create threads\n");
		vvideo_module_cleanup(be);
		ret = -EINVAL;
		goto out;
	}
	VVIDEO_LOG("Thread started\n");
	/*
	 * There must be one running and idle thread now.
	 * It can become non-idle only after vmq_links_start().
	 */
	VVIDEO_ASSERT_BE(vvideo_kref_read(&be->running_threads) == 1);
	VVIDEO_ASSERT_BE(vvideo_kref_read(&be->idle_threads) == 1);
	ret = vmq_links_start(be->links);
	if (ret) {
		VVIDEO_ERR("Could not start VMQ links (%d)\n", ret);
		vvideo_module_cleanup(be);
		goto out;
	}
	vvideo_proc_init(be);

out:
	VVIDEO_LOG_VERBOSE("DONE, ret %d\n", ret);
	VVIDEO_INFO("module loaded %s\n", ret ? "with errors" : "ok");
	return ret;
}

/*
 * vvideo_module_exit() is the last function called by linux driver framework.
 * It calls vvideo_module_cleanup() to free driver resources.
 */
static void __exit vvideo_module_exit(void)
{
	struct vvideo_be_t *be = &vvideo_be;

	VVIDEO_LOG("enter\n");
	vvideo_module_cleanup(be);
	VVIDEO_INFO("module unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("virtual V4L2 backend v2");

module_init(vvideo_module_init);
module_exit(vvideo_module_exit);
