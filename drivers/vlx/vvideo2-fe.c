// SPDX-License-Identifier: GPL-2.0+
/*
 ****************************************************************
 *
 *  Component: VLX virtual video v2 frontend driver
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
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif
#include <linux/debugfs.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#if KERNEL_VERSION(3, 19, 0) <= LINUX_VERSION_CODE
#include <media/videobuf2-v4l2.h>
#else
#include <media/videobuf2-core.h>
#endif

#include <nk/nkern.h>

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
#include "vfence2_kapi.h"
#endif

/*----- Local includes -----*/

#include <vlx/vvideo2_common.h>
#include "vvideo-util.h"
#include "vlx-vmq.h"
#include "vlx-vipc.h"

#ifndef VVIDEO_LOCAL_GID
#include "vdmaheap_kapi.h"
#endif

/*----- Configuration -----*/

    /* Enable extra checks for transmitted message lifecycle */
// #define VVIDEO_MSG_CHECKS

    /* Remember and display previous state of freed messages */
// #define VVIDEO_MSG_TRACKING

    /* Enable observation of VIDIOC_S_FMT ioctl()s */
// #define VVIDEO_S_FMT

    /* Enable per-device recording of requests */
// #define VVIDEO_RECORD

    /* Enable additional per-device recording of replies */
// #define VVIDEO_RECORD_REPLIES

/* Enable logging messages at all */
// #define VVIDEO_DEBUG

/* Enable low-level communications traces */
// #define VVIDEO_COMMS

/* Enable verbose traces */
// #define VVIDEO_VERBOSE

/* Enable DMABUF traces */
// #define VVIDEO_DMABUF

/* Enable asserts */
#define VVIDEO_ASSERTS

    /* Enable checksum verification of async ioctl request buffers */
#define VVIDEO_ASYNC_CHECKSUMS

/* Adjust device_caps at first VIDIOC_QUERYCAP */
// #define VVIDEO_DEVICE_CAPS

/*
 * Consider Decoded Pixel Buffer (DPB) file descriptors in
 * MFC user shared memory during DQBUF processing.
 */
#define VVIDEO_DPBS

/* Max DMABUFs per context */
#define VVIDEO_DMABUF_MAX 80

/* Log of max open contexts per video device */
#define VVIDEO_CTX_MAX_LOG 6

/* Max count of printed errors per category */
#define VVIDEO_ERR_MAX 10

/* Max count of printed warnings per category */
#define VVIDEO_WARN_MAX 10

#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
    /*
     * The version of vvideo driver should be able to update without
     * dependency between VMs.
     */
#define VVIDEO_FE_VERSION		PV_DRV_VERSION(3, 0, 0)
#define VVIDEO_DRV_VERSION		PV_DRV_VERSION(3, 0, 0)
#endif

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

#ifdef VVIDEO_RECORD
    /* Max count of records */
#define VVIDEO_RECORD_MAX	(8 * 1024)
    /* Whether to record by default */
#define VVIDEO_RECORD_DEFAULT	0
#endif

    /* vvideo_ioctl_format() and vvideo_req_format() buffer size */
#define VVIDEO_REQ_STRING	128

#ifndef VVIDEO_LOCAL_GID
#define VVIDEO_VDMAHEAP_GRACE_DELAY_MS 2000
#endif

/*----- Logging -----*/

#define VVIDEO_DRIVER "vvideo2-fe"
#define VVIDEO_MSG "VVIDEO2-FE: "

#ifdef VVIDEO_DEBUG
#define VVIDEO_LOG(format, args...)                                            \
	pr_info(VVIDEO_MSG "%s: " format, __func__, ##args)
#else
#define VVIDEO_LOG(format, args...)
#endif

#ifdef VVIDEO_COMMS
#define VVIDEO_LOG_COMMS(format, args...) VVIDEO_LOG(format, ##args)
#else
#define VVIDEO_LOG_COMMS(format, args...)
#endif

#ifdef VVIDEO_VERBOSE
#define VVIDEO_LOG_VERBOSE(format, args...) VVIDEO_LOG(format, ##args)
#else
#define VVIDEO_LOG_VERBOSE(format, args...)
#endif

#ifdef VVIDEO_DMABUF
#define VVIDEO_LOG_DMABUF(format, args...) VVIDEO_LOG(format, ##args)
#else
#define VVIDEO_LOG_DMABUF(format, args...)
#endif

#define VVIDEO_ERR(format, args...)                                            \
	pr_err(VVIDEO_MSG "%s: ERROR: " format, __func__, ##args)
#define VVIDEO_ERR_LIMITED(_counter, format, args...) \
	do {if (++(_counter) <= VVIDEO_ERR_MAX) \
		VVIDEO_ERR(format, ##args); } while (0)
#define VVIDEO_WARN(format, args...)                                           \
	pr_warn(VVIDEO_MSG "%s: WARNING: " format, __func__, ##args)
#define VVIDEO_WARN_LIMITED(_counter, format, args...)                         \
	do {                                                                   \
		if (++(_counter) <= VVIDEO_WARN_MAX)                           \
			VVIDEO_WARN(format, ##args);                           \
	} while (0)
#define VVIDEO_INFO(format, args...)                                           \
	pr_notice(VVIDEO_MSG format, ##args)

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
	struct kref kref;
	int saved_f_count; /* For information only */
	/* Status */
	_Bool valid;	/* Entry is valid */
	_Bool prepared;	/* Whether command has been issued */
	int queued;	/* How many times queued. Usually only 1 works */
	int running;	/* How many ioctls referencing are running */
};

struct vvideo_link_t {
	/* Fields set at init time once for all */
	struct vvideo_fe_t *fe; /* Parent */
	struct vvideo_configs_t configs; /* Configured devices */
	unsigned int dev_width; /* Bits for minor in be_context */
	unsigned int dev_mask; /* Mask for minor in be_context */
	vmq_link_t *link; /* Inter-VM communications */
	unsigned int number; /* Link number */
	char name[32]; /* For messages */
	vmq_xx_config_t xx_config; /* Transmit side */
	struct vvideo_dev_t *devs; /* Array of device descriptors */
	struct vvideo_msg_t *msgs; /* Array of msg descriptors */
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_connection *vfence2_connection;
#endif
	/* Work variables */
	_Bool link_on; /* Callback link_on received */
	unsigned int	 session;
	vipc_ctx_t vipc_ctx;
	wait_queue_head_t open_wait;
	atomic_t last_transaction;
	struct vvideo_msg_t *abort_msg;
#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
	u32 fe_version;
	u32 be_version;
#endif
	struct mutex abort_lock;
	spinlock_t msg_lock;	/* Protects vvideo_msg_t::kref */
	atomic_t msgs_used;	/* How many still allocated */
	_Bool	 pending_link_on;
	struct {
		unsigned int	link_on;
		unsigned int	link_off;
		unsigned int	link_off_completed;
		unsigned int	link_timeout;
	} stats;
	struct {
		unsigned int	failed_assert;
#ifdef VVIDEO_MSG_CHECKS
		unsigned int	msg_req_null;
		unsigned int	msg_still_used;
#endif
		unsigned int	msg_ignored_null_dev;
		unsigned int	wrong_event;
		unsigned int	wrong_minor;
	} warnings;
};

/*
 *  replied req_owned kref  state
 *     -        -      0    Free (I)
 *     0        1      1    Allocated (II)
 *     0        0      2    Sent (III)
 *     1        0      2    Replied (IVa)
 *     1        0      1    Reply-Ignored (IVb)
 *     1        1      1    Retrieved (V)
 *     -        -      0    Free again (I)
 */

/*
 * (kref A) Thread reference (m->pid != 0)
 * (kref B) VMQ reference (m->vmq != false)
 * (kref C) Abort reference (m->abort != false)
 * (kref D) Temporary reference (no variable)
 */

#define VVIDEO_MSG_THREAD	0x1
#define VVIDEO_MSG_VMQ		0x2
#define VVIDEO_MSG_ABORT	0x4

struct vvideo_msg_t {
	/* Fields set at init time once for all */
	unsigned int slot;
	char name[32];
	struct vvideo_link_t *link;
	/* Work variables */
	struct vvideo_ctx_t *ctx; /* Current */
	struct VVideo2Request *req; /* Current */
	enum VVideo2ReqNum req_num;
	_Bool req_owned; /* Must free */
	_Bool replied;	/* Reply came from backend */
	pid_t pid; /* Current, (kref A) vvideo_link_t::msg_lock */
	_Bool		 async;
	_Bool		 ignored;
	_Bool		 orphaned;
	_Bool		 cookie_found;
	_Bool vmq; /* (kref B) vvideo_link_t::msg_lock */
	_Bool abort; /* (kref C) vvideo_link_t::msg_lock */
	struct kref kref; /* vvideo_link_t::msg_lock */
#ifdef VVIDEO_ASYNC_CHECKSUMS
	unsigned long	 checksum;
#endif
	nku64_f cookie;
	/* Fields which used to be ioctl locals */
	struct {
		/* Arguments from user mode caller */
		unsigned int cmd;
		unsigned long arg;
		/* Work variables */
		u32 async_mask;
		void __user *orig_user_ptr;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		vfence2_id_t vfid;
#endif
	} ioctl;
#ifdef VVIDEO_MSG_TRACKING
	struct {
		_Bool		 req_owned;
		_Bool		 replied;
		pid_t		 pid;
		_Bool		 async;
		_Bool		 ignored;
		_Bool		 orphaned;
		_Bool		 cookie_found;
		_Bool		 vmq;
		_Bool		 abort;
	} old;
#endif
};

#ifdef VVIDEO_RECORD
#define VVIDEO_RECORDING	"recording"

struct vvideo_record_t {
	ktime_t		ktime;
	short		ctx;
	unsigned short	req_num; /* VVIDEO2_REQ_... */
	unsigned int	cmd;	/* VIDIOC_..., must be 32 bits */
	u32		arg[2];
};
#endif

/* Expressing this as _IOC_NR(VIDIOC_TRY_DECODER_CMD)+1 does not compile */
#define VVIDEO_MAX_IOCTL 98

/* Must be a power of 2, because we manipulate bit masks */
#define VVIDEO_CTX_MAX	(1 << VVIDEO_CTX_MAX_LOG)

struct vvideo_dev_t {
	/* Fields set at init time once for all */
	struct vvideo_link_t *link; /* Parent */
	unsigned int minor;
	char name[32];
	struct video_device *vfd;
	_Bool async;
	/* Work variables */
	struct mutex lock;
	atomic_t open_count;	/* For information only */
	spinlock_t ctx_lock;
	struct vvideo_ctx_t *ctx[VVIDEO_CTX_MAX];
	unsigned long reqbufs_0_ok; /* Indexed by enum v4l2_buf_type */
	unsigned long streamon_ok; /* Indexed by enum v4l2_buf_type */
#ifdef VVIDEO_DEVICE_CAPS
	int device_caps; /* 0=not checked, -1=were wrong, 1=were okay */
#endif
#ifdef VVIDEO_RECORD
	struct vvideo_record_t	 *records;
	atomic_t next;	/* Free running, scaled by VVIDEO_RECORD_MAX */
#endif
	struct vdev_errors {
		unsigned int arg_size;
		unsigned int copy_from_user;
		unsigned int ext_size;
		unsigned int fd2gid;
		unsigned int ext_gid;
		unsigned int fd_negative;
		unsigned int dmabuf_slots;
		unsigned int dma_buf_get;
		unsigned int dmabuf_export;
		unsigned int dmabuf_import;
		unsigned int dma_buf_fd;
		unsigned int not_dmabuf;
		unsigned int copy_to_user;
		unsigned int xirq_attach;
		unsigned int remap;
		unsigned int gid_invalid;
		unsigned int find_dma_buf;
		unsigned int epipe;
		unsigned int msg_get;
		unsigned int lost_call;
		unsigned int ctx_kmalloc;
		unsigned int ctx_max;
		unsigned int ctx_open;
		unsigned int req_open;
		unsigned int edid_blocks;
		unsigned int kmem_cache_alloc;
		unsigned int poll_find;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		unsigned int export_fence;
#endif
#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
		unsigned int get_be_version;
		unsigned int is_compatible;
		unsigned int open_release;
#endif
#ifdef VVIDEO_DPBS
		struct {
			unsigned int get;
			unsigned int import;
			unsigned int multiple;
			unsigned int vmap;
		} dpbs;
#endif
	} errors;
	struct vdev_warnings {
		unsigned int unknown_cid;
		unsigned int failed_assert;
		unsigned int unexport_gid;
		unsigned int abort_failed;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		unsigned int import_fence;
		unsigned int unexport_fence;
#endif
#ifdef VVIDEO_ASYNC_CHECKSUMS
		unsigned int async_checksum;
#endif
		unsigned int async_failure;
#ifdef VVIDEO_MSG_CHECKS
		unsigned int kref_not_2;
		unsigned int no_reply_kref_0;
#endif
		unsigned int poll_ctx;
		unsigned int poll_cookie;
#ifdef VVIDEO_DPBS
		struct {
			unsigned int index;
			unsigned int size;
		} dpbs;
 #endif
	} warnings;
	struct vdev_stats {
		unsigned int open;
		unsigned int release;
		unsigned int poll_1st;
		unsigned int poll_2nd;
		unsigned int abort;
		unsigned int fd2gid;
		unsigned int fd_multiple;
		unsigned int gid2fd;
		unsigned int init2;
		unsigned int init3;
		unsigned int init4;
		unsigned int initN;
		unsigned int export0;
		unsigned int export1;
		unsigned int exportN;
		unsigned int unexport0;
		unsigned int unexport1;
		unsigned int unexportN;
		unsigned int open_erestartsys;
		unsigned int open_eintr;
		unsigned int call_erestartsys;
		unsigned int call_eintr;
		unsigned int ioctl_erestartsys;
		unsigned int ioctl_error;
		unsigned int poll_erestartsys;
		unsigned int no_abort_msg;
		unsigned int async_msg;
		unsigned int sync_msg;
		unsigned int async_reply;
#ifdef VVIDEO_ASYNC_CHECKSUMS
		unsigned int reqbufs_0;
#endif
		unsigned int ignored_reply;
		unsigned int lost_open;
		unsigned int streamoff_pending;
		unsigned int poll_event;
		unsigned int fences_ok;
		unsigned int fences_removed;
		unsigned int poll_stale;
		unsigned int g_gid;
		unsigned int s_gid;
		unsigned int g_ext_gid;
		unsigned int s_ext_gid;
		unsigned int try_ext_gid;
		struct {
			unsigned int nr[VVIDEO_MAX_IOCTL];
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
			unsigned int in_fence;
			unsigned int out_fence;
#endif
			unsigned int single_planar;
			unsigned int other;
		} ioctl;
#ifdef VVIDEO_DPBS
		struct {
			unsigned int s_ctrl;
			unsigned int import;
		} dpbs;
#endif
		unsigned int deleted_gid;
	} stats;
};

struct vvideo_ctx_iter_t {
	struct vvideo_dev_t *dev;
	int ctx_num;
};

#define VVIDEO_ASYNC_MASK	"async_mask"

    /* Bits inside vvideo_fe_t::async_mask */
#define VVIDEO_ASYNC_S_CTRL	0x1
#define VVIDEO_ASYNC_STREAMON	0x2
#define VVIDEO_ASYNC_REQBUFS_0	0x4
#define VVIDEO_ASYNC_S_CROP	0x8

#define VVIDEO_ASYNC_MASK_DEFAULT	(VVIDEO_ASYNC_S_CTRL | \
				 VVIDEO_ASYNC_STREAMON | \
				 VVIDEO_ASYNC_REQBUFS_0 | \
				 VVIDEO_ASYNC_S_CROP)

/* One per driver */
struct vvideo_fe_t {
	/* Fields set at init time once for all */
#ifdef CONFIG_PROC_FS
	struct vvideo_fe_t *proc_normal; /* Must be first */
	struct vvideo_fe_t *proc_ext;
#endif
	vmq_links_t *links;
	unsigned int link_count;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	struct vfence2_client *vfence2_client;
#endif
#ifndef VVIDEO_LOCAL_GID
	struct vdmaheap_client *vdmaheap_client;
#endif
	int dev_num; /* number of configured devices */
#ifdef CONFIG_PROC_FS
	_Bool proc_exists; /* /proc/nk/VVIDEO_DRIVER has been created */
#endif
	struct v4l2_device v4l2_dev;
	_Bool v4l2_dev_registered;
	struct platform_device *pdev;
	/* /sys/kernel/debug/VVIDEO_DRIVER */
	struct dentry *debugfs;
	_Bool work_inited;
	/* Work variables */
	struct mutex mutex;
	/* /sys/kernel/debug/VVIDEO_DRIVER/VVIDEO_ASYNC_MASK */
	u32 async_mask;
#ifdef VVIDEO_RECORD
	/* /sys/kernel/debug/VVIDEO_DRIVER/VVIDEO_RECORDING */
	u32 recording;
#endif
	struct work_struct work; /* Process VMQ callbacks */
	atomic_t pending_releases;
	atomic_t pending_link_ons;
	_Bool sysconf_notify;
	struct kmem_cache *poll_kmem_cache;
	struct {
		unsigned int debugfs_create;
		unsigned int async_mask_create;
#ifdef VVIDEO_RECORD
		unsigned int recording_create;
#endif
	} warnings;
	struct {
		unsigned int pending_releases;
		unsigned int done_pending_releases;
		unsigned int pending_link_ons;
		unsigned int done_pending_link_ons;
	} stats;
};

/* One per active select/poll in context */
struct vvideo_poll_t {
	struct vvideo_ctx_t *ctx;
	wait_queue_head_t wqh;
	unsigned long requested_events;
	unsigned int cookie;
	atomic_t users;
	struct vvideo_poll_t *next;
};

/* One per open file */
struct vvideo_ctx_t {
	/* Fields set at open time once for all */
	struct vvideo_dev_t *dev;
	struct file *file;
	char name[32];
	struct v4l2_fh fh;
	unsigned int be_context;
	unsigned int num;
	struct {
		pid_t pid;
		char comm[TASK_COMM_LEN];
		/* Calling function is always v4l2_open() */
	} opener;
	/* Work fields */
	struct vvideo_dmabuf_t dma_bufs[VVIDEO_DMABUF_MAX];
	unsigned int dma_buf_last;
	struct kref kref;
	_Bool pending_release;
	/* Support for deferred dmabuf cache cleaning */
	unsigned int streamoff_pending; /* vvideo_dev_t::lock */
	int running_dqbufs;
	/* Support for polling */
	struct {
		spinlock_t spinlock;
		struct vvideo_poll_t *list;
		atomic_t last_cookie;
	} poll;
#ifdef VVIDEO_DPBS
	struct {
		int fd;
		struct dma_buf *dma_buf;
		struct mfc_dec_dpb_ref_info *info;
	} dpbs;
#endif
};

#define vvideo_fh_to_vvideo_ctx(x) container_of(x, struct vvideo_ctx_t, fh)

struct vvideo_call_t {
	unsigned int cmd;
	int (*before)(struct vvideo_msg_t *m);
	int (*after)(struct vvideo_msg_t *m, int ret);
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

#define VVIDEO_FOR_ALL_MSGS(_link, _i, _m)                                     \
	for ((_i) = 0, (_m) = (_link)->msgs;                                   \
	     (_i) < (_link)->xx_config.msg_count; ++(_i), ++(_m))

/*----- Checks of ioctl args fitting pmem buffers -----*/

#define VVIDEO2_REQ_IOCTL_TOTAL_SIZE                                           \
	(VVIDEO2_REQ_IOCTL_ARG_SIZE + VVIDEO2_REQ_IOCTL_EXT_SIZE)

#define VVIDEO_CHECK_SIZEOF(a, b) VVIDEO_STATIC_ASSERT(sizeof(a) <= (b))
#define VVIDEO_CHECK_SIZEOF1(a)                                                \
	VVIDEO_STATIC_ASSERT(sizeof(a) <= VVIDEO2_REQ_IOCTL_ARG_SIZE)
#define VVIDEO_CHECK_SIZEOF2(a)                                                \
	VVIDEO_STATIC_ASSERT(sizeof(a) >  VVIDEO2_REQ_IOCTL_ARG_SIZE &&        \
			     sizeof(a) <= VVIDEO2_REQ_IOCTL_TOTAL_SIZE)

VVIDEO_CHECK_SIZEOF1(struct v4l2_buffer);
VVIDEO_CHECK_SIZEOF1(struct v4l2_capability);
VVIDEO_CHECK_SIZEOF1(struct v4l2_control);
VVIDEO_CHECK_SIZEOF1(struct v4l2_crop);
VVIDEO_CHECK_SIZEOF1(struct v4l2_cropcap);
VVIDEO_CHECK_SIZEOF1(struct v4l2_decoder_cmd);
VVIDEO_CHECK_SIZEOF1(struct v4l2_edid);
VVIDEO_CHECK_SIZEOF1(struct v4l2_encoder_cmd);
VVIDEO_CHECK_SIZEOF1(struct v4l2_event_subscription);
VVIDEO_CHECK_SIZEOF1(struct v4l2_exportbuffer);
VVIDEO_CHECK_SIZEOF1(struct v4l2_ext_control);
VVIDEO_CHECK_SIZEOF1(struct v4l2_ext_controls);
VVIDEO_CHECK_SIZEOF1(struct v4l2_fmtdesc);
VVIDEO_CHECK_SIZEOF1(struct v4l2_fract);
VVIDEO_CHECK_SIZEOF1(struct v4l2_framebuffer);
VVIDEO_CHECK_SIZEOF1(struct v4l2_frmsizeenum);
VVIDEO_CHECK_SIZEOF1(struct v4l2_input);
VVIDEO_CHECK_SIZEOF1(struct v4l2_output);
VVIDEO_CHECK_SIZEOF1(struct v4l2_pix_format);
VVIDEO_CHECK_SIZEOF1(struct v4l2_plane);
VVIDEO_CHECK_SIZEOF1(struct v4l2_plane_pix_format);
VVIDEO_CHECK_SIZEOF1(struct v4l2_queryctrl);
VVIDEO_CHECK_SIZEOF1(struct v4l2_querymenu);
VVIDEO_CHECK_SIZEOF1(struct v4l2_requestbuffers);
VVIDEO_CHECK_SIZEOF1(struct v4l2_selection);
VVIDEO_CHECK_SIZEOF1(struct v4l2_subdev_crop);
VVIDEO_CHECK_SIZEOF1(struct v4l2_subdev_format);
VVIDEO_CHECK_SIZEOF1(struct v4l2_subdev_frame_interval);
VVIDEO_CHECK_SIZEOF1(struct v4l2_subdev_frame_interval_enum);
VVIDEO_CHECK_SIZEOF1(struct v4l2_subdev_frame_size_enum);
VVIDEO_CHECK_SIZEOF1(struct v4l2_subdev_mbus_code_enum);
VVIDEO_CHECK_SIZEOF1(struct v4l2_timecode);
VVIDEO_CHECK_SIZEOF1(struct v4l2_vbi_format);
VVIDEO_CHECK_SIZEOF1(struct v4l2_window);
VVIDEO_CHECK_SIZEOF2(struct v4l2_event);
VVIDEO_CHECK_SIZEOF2(struct v4l2_format);
VVIDEO_CHECK_SIZEOF2(struct v4l2_pix_format_mplane); /* part of v4l2_format */
VVIDEO_CHECK_SIZEOF2(struct v4l2_streamparm);

struct vvideo_v4l2_exportbuffer {
	struct v4l2_exportbuffer eb;
	struct vvideo_dmabuf_gid_t gid;
};

VVIDEO_CHECK_SIZEOF1(struct vvideo_v4l2_exportbuffer);

/*----- Common code -----*/

#define VVIDEO_LINK(link) (*(struct vvideo_link_t **)(link))
#define VVIDEO_LINKS_FE(links) (*(struct vvideo_fe_t **)(links))

#include "vvideo2-common.c"

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
	return vdmaheap_kapi_export(ctx->dev->link->fe->vdmaheap_client, dma_buf,
				&gid->gid, &gid->cred);
#endif
}

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
	ret = vdmaheap_kapi_import(ctx->dev->link->fe->vdmaheap_client, gid->gid,
				&gid->cred, dma_buf);
	if (ret < 0)
		VVIDEO_ERR_LIMITED(ctx->dev->errors.dmabuf_import,
			"ctx %s vdmaheap_kapi_import(%s) failed (%d)\n",
			ctx->name, vvideo_dmabuf_gid_format(gid, buf,
			sizeof(buf)), ret);
	return ret;
#endif
}

static void vvideo_dmabuf_unexport(struct vvideo_ctx_t *ctx,
				const struct vvideo_dmabuf_gid_t *gid)
{
	/* Decreases dma_buf reference count */
#ifdef VVIDEO_LOCAL_GID
	struct dma_buf *dma_buf = (void *) gid->dma_buf;

	dma_buf_put(dma_buf); /* void */
#else
	char buf[VVIDEO_GID_STRING];
	int ret;

	ret = vdmaheap_kapi_unexport(ctx->dev->link->fe->vdmaheap_client, gid->gid,
		&gid->cred);
	if (ret < 0)
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.unexport_gid,
			"ctx %s vdmaheap_kapi_unexport(%s) failed (%d)\n",
			ctx->name, vvideo_dmabuf_gid_format(gid, buf,
			sizeof(buf)), ret);
#endif
}

/* Initial kref is 2, for VIDIOC_QBUF needs */

static inline void vvideo_dmabuf_init(struct vvideo_dmabuf_t *d,
				      struct vvideo_ctx_t *ctx,
				      const struct vvideo_dmabuf_gid_t *gid,
				      struct dma_buf *dma_buf, int fd,
				      int buf_index, int plane,
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

/*
 * Called on VIDIOC_QBUF, VIDIOC_PREPARE_BUF and selected VIDIOC_S_CTRL.
 * We check flags and increase counters and kref accordingly.
 * vvideo_dev_t::lock must be Locked.
 */

static int vvideo_dmabuf_fd2gid(struct vvideo_ctx_t *ctx, int fd, int buf_index,
				int plane, enum v4l2_buf_type buf_type,
				struct vvideo_dmabuf_gid_t *gid,
				unsigned int flags)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_dmabuf_t *last_free = NULL;
	struct vvideo_dmabuf_t *d;
	int d_counter;
	struct dma_buf *dma_buf;
	char buf[VVIDEO_GID_STRING];
	int ret;
	int f_count, f_count2;

	(void) buf;
	++vvideo_dev->stats.fd2gid;
	/* fd -1 could match unused slots in array, so refuse it here */
	if (!VVIDEO_FD_VALID(fd)) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.fd_negative,
			"ctx %s fd %d negative\n", ctx->name, fd);
		return -EINVAL;
	}
	VVIDEO_FOR_ALL_DMABUFS_REVERSE(ctx, d)
	{
		if (!d->valid) {
			last_free = d;
			continue;
		}
		if (d->fd != fd)
			continue;
		++vvideo_dev->stats.fd_multiple;
		VVIDEO_LOG(
	"ctx %s fd %d kref %d gid %s dma_buf %p (%d/%d %d %s) flags %#x\n",
			   ctx->name, fd, (int)vvideo_kref_read(&d->kref),
			   vvideo_dmabuf_gid_format(&d->gid, buf, sizeof(buf)),
			   d->dma_buf, d->buf_index, d->plane, d->buf_type,
			   vvideo_v4l2_buf_type_short(d->buf_type), flags);
		VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&d->kref) >= 1);
		/*
		 * Check if same dma_buf. This should never error
		 * because we increased f_count if it is in our table.
		 */
		dma_buf = dma_buf_get(fd); /* Increases f_count */
		if (!IS_ERR(dma_buf)) {
			VVIDEO_ASSERT(vvideo_dev, dma_buf == d->dma_buf);
			/* Release what we just obtained rather than expected */
			dma_buf_put(dma_buf); /* void */
		}
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
			else
				kref_get(&d->kref);
		}
		if (flags & VVIDEO_DMABUF_RUNNING) {
			++d->running;
			kref_get(&d->kref);
		}
		*gid = d->gid;
		return 0;
	}
	d = last_free;	/* Using last_free would be confusing */
	if (!d) {
		/* Error also accounted by caller */
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dmabuf_slots,
			"ctx %s out of slots for fd %d\n", ctx->name, fd);
		return -ENOMEM;
	}
	dma_buf = dma_buf_get(fd); /* Increases f_count */
	if (IS_ERR(dma_buf)) {
		/* Error also accounted by caller */
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dma_buf_get,
			"ctx %s dma_buf_get(%d) failed (%d)\n", ctx->name,
			   fd, (int)PTR_ERR(dma_buf));
		return PTR_ERR(dma_buf);
	}
	/*
	 * There should be 1 dma_buf reference because of the original
	 * fd and 1 reference added by dma_buf_get(), so we should
	 * have at least 2 in total.
	 * vvideo_dmabuf_export() increases reference count.
	 */
	f_count = file_count(dma_buf->file);
	VVIDEO_ASSERT(vvideo_dev, f_count >= 2);
	if (f_count == 2)
		++vvideo_dev->stats.init2;
	else if (f_count == 3)
		++vvideo_dev->stats.init3;
	else if (f_count == 4)
		++vvideo_dev->stats.init4;
	else
		++vvideo_dev->stats.initN;
	ret = vvideo_dmabuf_export(ctx, dma_buf, gid);
	if (ret) {
		/* Error also accounted by caller */
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dmabuf_export,
			"ctx %s vvideo_dmabuf_export() fd %d failed (%d)\n",
			ctx->name, fd, ret);
		dma_buf_put(dma_buf); /* void */
		return ret;
	}
	/*
	 * There should be 1 dma_buf reference because of the original
	 * fd, 1 reference added by dma_buf_get() and 1 reference added
	 * by vvideo_dmabuf_export(), so we should have at least 3
	 * in total.
	 */
	VVIDEO_ASSERT(vvideo_dev, file_count(dma_buf->file) >= 3);
	/*
	 * vdmaheap_kapi_export() mostly adds a reference the first time
	 * it sees a buffer. Keep statistics.
	 */
	f_count2 = (int)file_count(dma_buf->file);
	if (f_count2 == f_count)
		++vvideo_dev->stats.export0;
	else if (f_count2 == f_count + 1)
		++vvideo_dev->stats.export1;
	else
		++vvideo_dev->stats.exportN;
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
	d->valid = true;
	ctx->dma_buf_last = d - ctx->dma_bufs;
	VVIDEO_LOG_DMABUF(
		"ctx %s Mapped %d to gid %s flags %#x\n", ctx->name, fd,
		vvideo_dmabuf_gid_format(gid, buf, sizeof(buf)), flags);
	return 0;
}

/*
 * Release on last user going away.
 * vvideo_dev_t::lock must be Locked.
 */

static noinline void vvideo_dmabuf_kref_release(struct kref *kref)
{
	struct vvideo_dmabuf_t *d =
		container_of(kref, struct vvideo_dmabuf_t, kref);
	struct vvideo_ctx_t *ctx = d->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	const int f_count = file_count(d->dma_buf->file);
	int f_count2;
	char buf[VVIDEO_GID_STRING];

	(void) buf;
	VVIDEO_LOG_DMABUF(
	    "ctx %s gid %s dma_buf %p fd %d kref %d f_count %d (%d/%d %d %s)\n",
			  ctx->name, vvideo_dmabuf_gid_format(&d->gid,
			  buf, sizeof(buf)), d->dma_buf, d->fd,
			  (int)vvideo_kref_read(&d->kref), f_count,
			  d->buf_index, d->plane, d->buf_type,
			  vvideo_v4l2_buf_type_short(d->buf_type));
	/*
	 * At final release, the process may have already closed
	 * the file descriptor, so we are the last user.
	 */
	VVIDEO_ASSERT(vvideo_dev, f_count >= 2);
	/* Decreases dma_buf reference count */
	/* Warning already accounted and logged if failure */
	vvideo_dmabuf_unexport(ctx, &d->gid);
	/*
	 * vdmaheap_kapi_unexport() almost never removes a reference.
	 * Keep statistics.
	 */
	f_count2 = file_count(d->dma_buf->file);
	if (f_count2 == f_count)
		++vvideo_dev->stats.unexport0;
	else if (f_count2 == f_count - 1)
		++vvideo_dev->stats.unexport1;
	else
		++vvideo_dev->stats.unexportN;
	/* Reference can drop to 1 e.g. in case of backend reboots */
	VVIDEO_ASSERT(vvideo_dev, file_count(d->dma_buf->file) >= 1);
	/* Release reference resulting from initial dma_buf_get(fd) */
	dma_buf_put(d->dma_buf); /* void */
	/*
	 * Do not access d->dma_buf anymore to check f_count,
	 * as it could have disappeared, if somehow the user-provided
	 * fd was closed in parallel.
	 */
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

	(void) vvideo_dev;
	(void) buf;
	VVIDEO_LOG_DMABUF(
		"gid %s dma_buf %p fd %d kref %d f_count %d %s flags %#x %d\n",
		vvideo_dmabuf_gid_format(&d->gid, buf, sizeof(buf)),
		d->dma_buf, d->fd, (int)vvideo_kref_read(&d->kref),
		file_count(d->dma_buf->file),
		vvideo_v4l2_buf_type_short(d->buf_type), flags, one);
	VVIDEO_LOG_DMABUF(
		"prepared %d queued %d running %d\n",
		d->prepared, d->queued, d->running);

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
 * Removes references to "gid" in case of failed VIDIOC_QBUF
 * or VIDIOC_PREPARE_BUF preparation.
 * vvideo_dev_t::lock must be Locked.
 */

static noinline void vvideo_dmabuf_release_gid(struct vvideo_ctx_t *ctx,
				      const struct vvideo_dmabuf_gid_t *gid,
				      unsigned int flags)
{
	struct vvideo_dmabuf_t *d;
	int d_slot;

	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		if (!d->valid)
			continue;
		if (vvideo_dmabuf_gid_same(&d->gid, gid))
			vvideo_dmabuf_release_flags(d, flags, true);
	}
}

/*
 * Called on return from VIDIOC_QBUF, VIDIOC_DQBUF, VIDIOC_PREPARE_BUF
 * and selected VIDIOC_G_CTRL and VIDIOC_S_CTRL.
 * vvideo_dev_t::lock must be Locked.
 */

static noinline int vvideo_dmabuf_gid2fd(struct vvideo_ctx_t *ctx,
					 const struct vvideo_dmabuf_gid_t *gid,
					 const unsigned int flags,
					 const unsigned int cmd)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_dmabuf_t *deleted = NULL;
	struct vvideo_dmabuf_t *d;
	int d_counter;
	char buf[VVIDEO_GID_STRING];

	++vvideo_dev->stats.gid2fd;
	if (!VVIDEO_GID_VALID(gid)) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.gid_invalid,
			"ctx %s %s gid %s invalid\n", ctx->name,
			vvideo_ioctl_name(cmd),
			vvideo_dmabuf_gid_format(gid, buf, sizeof(buf)));
		return -EINVAL;
	}
	VVIDEO_FOR_ALL_DMABUFS_REVERSE(ctx, d)
	{
		int fd;

		if (!vvideo_dmabuf_gid_same(&d->gid, gid))
			continue;
		if (!d->valid) {
			if (!(flags & VVIDEO_DMABUF_DELETED))
				continue;
			if (!deleted)
				deleted = d;
			continue;
		}
		fd = d->fd;
		VVIDEO_LOG("ctx %s %s gid[%d] %s fd %d flags %#x\n", ctx->name,
			vvideo_ioctl_name(cmd),
			d->plane, vvideo_dmabuf_gid_format(&d->gid, buf,
			sizeof(buf)), d->fd, flags);
		vvideo_dmabuf_release_flags(d, flags, true);
		return fd;
	}
	if (deleted) {
		d = deleted;
		/* Only access safe fields */
		VVIDEO_LOG_DMABUF(
			"ctx %s %s deleted gid %s fd %d flags %#x index %d\n",
			ctx->name, vvideo_ioctl_name(cmd),
			vvideo_dmabuf_gid_format(&d->gid, buf, sizeof(buf)),
			d->fd, flags, d->buf_index);
		++vvideo_dev->stats.deleted_gid;
		return d->fd;
	}
	VVIDEO_ERR_LIMITED(vvideo_dev->errors.find_dma_buf,
		"ctx %s %s could not find gid %s\n", ctx->name,
		vvideo_ioctl_name(cmd),
		vvideo_dmabuf_gid_format(gid, buf, sizeof(buf)));
	return -ENOENT;
}

/*
 * On STREAMOFF, which applies to one v4l2_buf_type only.
 * Called only after all DQBUF have finished running.
 * Only release PREPARED and QUEUED references.
 * vvideo_dev_t::lock must be Locked.
 */

static noinline void vvideo_dmabuf_release_buf_type(struct vvideo_ctx_t *ctx,
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
 * Release all dmabufs because context is closed/released.
 * We do not care about still running BE requests, because
 * they have their own ctx->kref, so ctx is not going to
 * be released too soon.
 * vvideo_dev_t::lock state is Locked.
 */

static noinline void vvideo_dmabuf_release_all(struct vvideo_ctx_t *ctx)
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

/*----- VIPC extensions -----*/

static _Bool vipc_list_wait_ex(vipc_list_t *list, vipc_waiter_t *waiter,
			       _Bool interruptible)
{
	set_current_state(interruptible ? TASK_INTERRUPTIBLE :
					  TASK_UNINTERRUPTIBLE);
	while (!waiter->occurred) {
		const long ret = schedule_timeout(HZ);

		if (signal_pending(current)) {
			VVIDEO_LOG(
			"interruptible %d occurred %d pid %d got signal\n",
			       interruptible, waiter->occurred, current->pid);
			/*
			 * Sometimes we get a spurious wakeup even
			 * for TASK_UNINTERRUPTIBLE waits. Do not
			 * return, or the caller will be confused and
			 * might want to reuse the still busy message.
			 */
			if (interruptible)
				return true;
		}
		if (ret <= 0) {
			struct vvideo_link_t *vl = container_of(list,
				struct vvideo_link_t, vipc_ctx.list);

			++vl->stats.link_timeout;
			VVIDEO_LOG("link %s send timeout (%ld)\n",
				vl->name, ret);
			vmq_msg_send_wake(vl->link);
		}
		set_current_state(interruptible ? TASK_INTERRUPTIBLE :
						  TASK_UNINTERRUPTIBLE);
	}
	set_current_state(TASK_RUNNING);
	return false;
}

/* Interrupt-level callback */

static int vipc_ctx_process_reply_ex(vipc_ctx_t *vipc_ctx,
					struct vvideo_msg_t *m)
{
	unsigned long spinlock_flags;
	vipc_result_t *result;

	VIPC_LIST_LOCK(&vipc_ctx->list, spinlock_flags);
	if (!m->cookie) {
		VIPC_LIST_UNLOCK(&vipc_ctx->list, spinlock_flags);
		return -1;
	}
	VVIDEO_LOG("link %s request %s\n",
		m->link->name, vvideo_req_full(m->req));
	m->cookie_found = true;
	result = (void *) m->cookie;	/* u64 to void* */
	result->reply = (void *) m->req;	/* request* to nku64_f* */
	vipc_waiter_wakeup(&result->waiter);
	VIPC_LIST_UNLOCK(&vipc_ctx->list, spinlock_flags);
	return 0;
}

void *vipc_ctx_call_ex(vipc_ctx_t *vipc_ctx, void *req, nku64_f *vipc_cookie,
			  _Bool interruptible,
			  void (*abort_handler)(void *, void *), void *cookie)
{
	vipc_result_t result;
	unsigned long spinlock_flags;

	vipc_result_init(&result, vipc_ctx);
	vipc_list_add(&vipc_ctx->list, vipc_cookie, &result.waiter);
	vmq_msg_send(vipc_ctx->link, req);

	while (vipc_list_wait_ex(&vipc_ctx->list, &result.waiter,
	       interruptible)) {
		/* Only returns "true" if interruptible */
		if (abort_handler)
			abort_handler(cookie, req);
		interruptible = false;
	}
	/*
	 * Erase cookie inside vvideo_msg_t to prevent late
	 * vipc_ctx_process_reply_ex() from dereferencing
	 * a dangling vipc_result_t pointer.
	 * Erase the result.waiter.task pointer, which
	 * could become dangling after we exit from here.
	 */
	VIPC_LIST_LOCK(&vipc_ctx->list, spinlock_flags);
	if (!result.waiter.detached)
		list_del(&result.waiter.list);
	*vipc_cookie = 0; /* Erase cookie for message */
	result.waiter.task = NULL;
	VIPC_LIST_UNLOCK(&vipc_ctx->list, spinlock_flags);
	return result.reply;
}

/*----- Code -----*/

    /* vvideo_msg_format() buffer size */
#ifdef VVIDEO_MSG_TRACKING
#define VVIDEO_MSG_STRING	256
#else
#define VVIDEO_MSG_STRING	128
#endif

static const char vvideo_msg_legend[] =
	"PID-- Name- L.Mi ORAIFCKVB Transacti Request-- Result-- Ctx--- R";

/*
 * Caller must own m->kref, so "RC" will be shown with an extra +1
 * versus reality.
 */

static char *vvideo_msg_format(const struct vvideo_msg_t *m, char *buf,
			       size_t buflen)
{
	const struct vvideo_ctx_t *ctx = m->ctx;
	const struct VVideo2Request *req = m->req;

	snprintf(buf, buflen,
		"%5d %-5s %4s %c%c%c%c%c%c%c%c%c %9d %-9s %8x %6x %1d"
#ifdef VVIDEO_MSG_TRACKING
		"\n    %5d ----- ---- %c%c%c%c%c%c-%c%c"
#endif
		,
		m->pid, m->name, ctx ? ctx->dev->name : "None",
		m->req_owned       ? 'O' : '.',
		m->replied         ? 'R' : '.',
		m->async           ? 'A' : '.',
		m->ignored         ? 'I' : '.',
		m->orphaned        ? 'F' : '.',
		m->cookie_found    ? 'C' : '.',
		m->cookie          ? 'K' : '.',
		m->vmq             ? 'V' : '.',
		m->abort           ? 'B' : '.',
		req ? req->transaction : 0,
		req ? vvideo_req_full(req) : "",
		req ? req->result : 0,
		req ? req->be_context : 0,
		vvideo_kref_read(&m->kref)
#ifdef VVIDEO_MSG_TRACKING
		, m->old.pid,
		m->old.req_owned       ? 'O' : '.',
		m->old.replied         ? 'R' : '.',
		m->old.async           ? 'A' : '.',
		m->old.ignored         ? 'I' : '.',
		m->old.orphaned        ? 'F' : '.',
		m->old.cookie_found    ? 'C' : '.',
		m->old.vmq             ? 'V' : '.',
		m->old.abort           ? 'B' : '.'
#endif
		);
	return buf;
}

/*
 * Does not release msg back to pool, but makes unused.
 * Does not touch kref fields: pid, vmq, abort.
 * Called from interrupt handler too.
 */

static void vvideo_msg_clean(struct vvideo_msg_t *m, struct VVideo2Request *req)
{
#ifdef VVIDEO_MSG_TRACKING
	if (!req) {
#define X(x)	m->old.x = m->x
		X(req_owned);
		X(replied);
		X(pid);
		X(async);
		X(ignored);
		X(orphaned);
		X(cookie_found);
		X(vmq);
		X(abort);
#undef X
	}
#endif
	m->ctx = NULL;
	m->req = req;
	/* Enter state Allocated (II) */
	m->req_owned = true;
	m->replied = false;
	/* Do not clear m->pid */
	m->async = false;
	m->ignored = false;
	m->orphaned = false;
	m->cookie_found = false;
	/* Do not clear m->vmq */
	/* Do not clear m->abort */
	m->cookie = 0;
}

static void vvideo_link_dec_msgs_used(struct vvideo_link_t *vl)
{
	if (atomic_dec_and_test(&vl->msgs_used) && vl->pending_link_on)
		/* msgs_used now 0, can switch from off to link_on */
		schedule_work(&vl->fe->work);
}

/*
 * No lock needs to be taken and no lock should,
 * because can sleep waiting for free msg.
 * ctx can be NULL.
 */

static struct vvideo_msg_t *vvideo_link_msg_get(struct vvideo_link_t *vl,
					       struct vvideo_ctx_t *ctx,
					       _Bool nonblocking)
{
	int ret;
	void *msg;
	struct vvideo_msg_t *m;
#ifdef VVIDEO_MSG_CHECKS
	char buf[VVIDEO_MSG_STRING];
#endif
	unsigned long spinlock_flags;

	atomic_inc(&vl->msgs_used);
	if (!vl->link_on) {
		vvideo_link_dec_msgs_used(vl);
		/* Error message will be issued by caller */
		return ERR_PTR(-ECONNABORTED);
	}
	ret = vmq_msg_allocate_ex(vl->link, 0 /*data_offset*/, &msg,
				  0 /*data_offset*/, nonblocking);
	if (ret) {
		vvideo_link_dec_msgs_used(vl);
		/* Error message will be issued by caller */
		return vl->link_on ? ERR_PTR(ret) : ERR_PTR(-ECONNABORTED);
	}
	m = vl->msgs + vmq_msg_slot(vl->link, msg);

#ifdef VVIDEO_MSG_CHECKS
	if (m->ctx || m->req || m->pid || m->async || m->ignored || m->vmq ||
			m->orphaned || m->cookie_found || m->abort ||
			vvideo_kref_read(&m->kref)) {
		WARN(++vl->warnings.msg_still_used <= VVIDEO_WARN_MAX,
			"msg used line %d for msg %p: %s\n",
			__LINE__, m, vvideo_msg_format(m, buf, sizeof(buf)));
	}
#endif
	vvideo_msg_clean(m, msg);
	m->ctx = ctx;
	if (ctx)
		kref_get(&ctx->kref);
	spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
	/* (kref A) get */
	m->pid = current->pid;
	kref_init(&m->kref);
	spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);
	return m;
}

static void vvideo_ctx_abort(void *cookie, void *msg);

static void vvideo_msg_release(struct vvideo_msg_t *m, unsigned int flags);

#ifdef VVIDEO_RECORD
static void vvideo_dev_record(struct vvideo_dev_t *vvideo_dev, int ctx,
				const struct VVideo2Request *req)
{
	const struct VVideo2Ioctl *req_ioctl = &req->u.ioctl;
	const unsigned int curr = atomic_inc_return(&vvideo_dev->next) - 1;
	struct vvideo_record_t *r =
		vvideo_dev->records + curr % VVIDEO_RECORD_MAX;

	r->ktime = ktime_get();
	r->ctx   = ctx;
	r->req_num = req->req_num;
	r->cmd   = req_ioctl->cmd;
	memcpy(r->arg, req_ioctl->arg, sizeof(r->arg));
}
#endif

static int vvideo_msg_call(struct vvideo_msg_t *m, _Bool interruptible,
			      _Bool abortable)
{
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	struct VVideo2Request *req = m->req;
	const unsigned int transaction =
		atomic_inc_return(&vl->last_transaction);
	unsigned long spinlock_flags;
	void *reply;
	int req_result;

	req->req_num = m->req_num;
	req->flags = 0;
	if (m->async)
		req->flags |= VVIDEO2_REQ_ASYNC;
	if (ctx->file->f_flags & O_NONBLOCK)
		req->flags |= VVIDEO2_REQ_NONBLOCK;
	else
		req->flags |= VVIDEO2_REQ_BLOCK;
	req->result = VVIDEO2_RES_PENDING;
	req->be_context = ctx->be_context;
	req->transaction = transaction;

	/* This trace requires req->req_num to be set */
	VVIDEO_LOG_COMMS("ctx %s %s, req %d (%s)\n", ctx->name, m->name,
			 m->req_num, vvideo_req_full(req));
	/*
	 * Once vipc_ctx_call_ex() is invoked, we temporarily lose
	 * ownership of msg and must not vmq_return_msg_free()
	 * unless non-NULL is returned.
	 * The kref_get() signifies giving vvideo_cb_return_notify()
	 * the right to access the message, and vvideo_msg_release()
	 * it, if no thread to awaken could be found.
	 */
	VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&m->kref) >= 1);
	spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
	/* (kref B) get */
	kref_get(&m->kref);
	m->vmq = true;
	spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);
	VVIDEO_ASSERT(vvideo_dev, vvideo_kref_read(&m->kref) >= 2);
	/* Enter state Sent (III) */
	m->req_owned = false;
#ifdef VVIDEO_RECORD
	if (vl->fe->recording)
		vvideo_dev_record(vvideo_dev, ctx->num, req);
#endif
	if (m->async) {
		++vvideo_dev->stats.async_msg;
		/* Clear m->pid only at release time */
		vmq_msg_send_async(vl->vipc_ctx.link, req);
		return 0;
	}
	++vvideo_dev->stats.sync_msg;
	reply = vipc_ctx_call_ex(&vl->vipc_ctx, req, &m->cookie, interruptible,
				 abortable ? vvideo_ctx_abort : NULL, m);
	if (!reply) {
#ifdef VVIDEO_MSG_CHECKS
		if (vvideo_kref_read(&m->kref) <= 0) {
			char	buf[VVIDEO_MSG_STRING];

			WARN(++vvideo_dev->warnings.no_reply_kref_0 <=
				VVIDEO_WARN_MAX,
				"m->kref 0 line %d for msg %p: %s\n",
				__LINE__, m,
				vvideo_msg_format(m, buf, sizeof(buf)));
		}
#endif
		m->orphaned = true;
		VVIDEO_LOG("ctx %s, call %s interrupted\n", ctx->name,
			   vvideo_req_full(req));
		if (vl->link_on) {
			VVIDEO_ASSERT(vvideo_dev, interruptible);
			if (signal_pending(current)) {
				++vvideo_dev->stats.call_erestartsys;
				return -ERESTARTSYS;
			}
		}
		++vvideo_dev->stats.call_eintr;
		return -EINTR;
	}
#ifdef VVIDEO_MSG_CHECKS
	if (vvideo_kref_read(&m->kref) < 2) {
		char	buf[VVIDEO_MSG_STRING];

		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.kref_not_2,
			"m->kref not 2 line %d on reply for msg %p: %s\n",
			__LINE__, m, vvideo_msg_format(m, buf, sizeof(buf)));
	}
#endif
	/* Enter state Retrieved (V) */
	m->req_owned = true;
	/* m->replied was set by handler */
	req_result = req->result; /* Take snapshot */
	VVIDEO_ASSERT(vvideo_dev, reply == (void *) req);
	VVIDEO_LOG_COMMS("ctx %s %s AWAKEN, req %d (%s), result %d/%#x\n",
			 ctx->name, m->name, m->req_num,
			 vvideo_req_full(req), req_result, req_result);
	req->req_num = VVIDEO2_REQ_NONE;
	if (req_result == VVIDEO2_RES_PENDING) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.epipe,
			"ctx %s %s EPIPE\n", ctx->name, m->name);
		return -EPIPE;
	}
	if (req_result <= -ERESTARTSYS && !signal_pending(current))
		req_result = -EINTR;
	return req_result;
}

static void vvideo_ctx_kref_release(struct kref *kref);

/*
 * Called when m->kref goes to zero.
 * Called from interrupt handler too.
 */

static void vvideo_msg_kref_release(struct kref *kref)
{
	struct vvideo_msg_t *m = container_of(kref, struct vvideo_msg_t, kref);
	struct vvideo_link_t *vl = m->link;
	struct vvideo_fe_t *fe = vl->fe;
	struct VVideo2Request *req = m->req_owned || m->ignored ?
					m->req : NULL;
	/*
	 * We come in states Allocated (II), Reply-Ignored (IVb)
	 * or Retrieved (V), except kref has already been decremented
	 * to zero.
	 * Once we free "req", "m" could be immediately reused,
	 * so we need to zero it beforehand.
	 * m->ctx is not always set.
	 */
	if (m->ctx)
		kref_put(&m->ctx->kref, vvideo_ctx_kref_release);
	vvideo_msg_clean(m, NULL /*req*/);
	/*
	 * Only return msg if req_owned currently, i.e. not in call,
	 * or if ignored (will never become owned again).
	 * Note that m->req_owned is reset to "true" and m->ignored to
	 * "false" by vvideo_msg_clean(), so we read them beforehand.
	 */
	if (req)
		vmq_return_msg_free(vl->link, req);
	vvideo_link_dec_msgs_used(vl);
	if (atomic_read(&fe->pending_releases) > 0)
		schedule_work(&fe->work);
}

/* Called from interrupt handler too */

static void vvideo_msg_release(struct vvideo_msg_t *m, unsigned int flags)
{
	unsigned long spinlock_flags;
	struct vvideo_link_t	*vl = m->link;
	int counter = 0;

	spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
	if (flags & VVIDEO_MSG_THREAD) {
		/* (kref A) put */
		counter += m->pid != 0;
		m->pid = 0;
	}
	if (flags & VVIDEO_MSG_VMQ) {
		/* (kref B) put */
		counter += m->vmq;
		m->vmq = false;
	}
	if (flags & VVIDEO_MSG_ABORT) {
		/* (kref C) put */
		counter += m->abort;
		m->abort = false;
	}
	spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);
	while (counter-- > 0)
		kref_put(&m->kref, vvideo_msg_kref_release);
}

/* Send an abort using a pre-allocated and reserved msg */

static void vvideo_ctx_abort(void *msg0, void *req0)
{
	struct vvideo_msg_t *msg = msg0;
	struct vvideo_ctx_t *ctx = msg->ctx;
	struct VVideo2Request *req = req0;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	unsigned long spinlock_flags;
	struct vvideo_msg_t *m;
	int ret;

	VVIDEO_LOG("pid %d ctx %s call %s\n", current->pid, ctx->name,
		   vvideo_req_full(req));
	mutex_lock(&vl->abort_lock);
	++vvideo_dev->stats.abort;
	if (!vl->abort_msg) {
		/* After link_off. Abort will happen anyway */
		++vvideo_dev->stats.no_abort_msg;
		VVIDEO_LOG("link %s no abort_msg\n", vl->name);
		mutex_unlock(&vl->abort_lock);
		return;
	}
	m = vl->abort_msg;
	m->ctx = ctx;
	/* "ctx" was attached to "m" so increase kref */
	kref_get(&ctx->kref);

	/* (kref A) get */
	spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
	m->pid = current->pid;
	kref_get(&m->kref);
	spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);

	m->req->u.abort.transaction = req->transaction;
	m->req_num = VVIDEO2_REQ_ABORT;
	ret = vvideo_msg_call(m, false /*!interruptible*/,
				 false /*!abortable*/);
	if (ret && ret != -ESTALE)
		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.abort_failed,
				    "ctx %s could not abort %s (%d)\n",
				    ctx->name, vvideo_req_full(req), ret);
	/* Clean these fields to avoid /proc/nk/vvideo2-fe staleness */
	m->req->result = 0;
	m->req->be_context = 0;
	m->req->transaction = 0;
	/* "ctx" will be detached from "m", so put it */
	kref_put(&ctx->kref, vvideo_ctx_kref_release);
	/* Keep "m->req" */
	vvideo_msg_clean(m, m->req);
	/* (kref A) put */
	vvideo_msg_release(m, VVIDEO_MSG_THREAD);
	mutex_unlock(&vl->abort_lock);
}

    /*
     * Routine called when all requests have returned to VMQ,
     * so no req_owned requests remain and no request can become
     * req_owned.
     * Various threads can be running, using vvideo_msg_t
     * but without req_owned request.
     */
static void vvideo_link_msg_cleanup(struct vvideo_link_t *vl)
{
	unsigned long spinlock_flags;
	struct vvideo_msg_t *m;
	int m_slot;

	spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
	VVIDEO_FOR_ALL_MSGS(vl, m_slot, m)
	{
		/* Normally, there should be only 1 ref, if any */
		if (vvideo_kref_read(&m->kref) <= 0)
			continue;
		kref_get(&m->kref); /* (kref D) get */
		spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);
		VVIDEO_LOG("%s device %s kref %d req_owned %d replied %d\n",
			m->name, m->ctx ? m->ctx->dev->name : "None",
			vvideo_kref_read(&m->kref), m->req_owned,
			m->replied);
		/*
		 * vvideo_msg_kref_release() called from
		 * vvideo_msg_release() only returns a request
		 * back to VMQ if this request is currently req_owned
		 * or ignored.
		 * We should not try to return requests we did not yet
		 * get back from backend as replies.
		 * If state Sent (III).
		 */
		if (m->req_owned || m->replied)
			goto next;
		if (m->ctx)
			VVIDEO_ASSERT(m->ctx->dev, !m->ignored);
		/* (kref B) put, if any */
		vvideo_msg_release(m, VVIDEO_MSG_VMQ);
next:
		/* (kref D) put */
		kref_put(&m->kref, vvideo_msg_kref_release);
		spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
	}
	spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);
}

static _Bool vvideo_link_is_on(struct vvideo_link_t *vl)
{
	VVIDEO_LOG_VERBOSE("link %s link_on %d\n", vl->name,
			   vl->link_on);
	return vl->link_on;
}

static int vvideo_ctx_open(struct vvideo_ctx_t *ctx, unsigned int f_flags)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	struct vvideo_fe_t *fe = vl->fe;
	struct vvideo_msg_t *m;
	int ret;
#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
	struct pv_driver_header_v1_t *ver = NULL;
#endif
	/*
	 * Set be_context right away, so that /proc/nk/vvideo2-fe
	 * shows it when frontend waits for backend to link_on.
	 */
	ctx->be_context = vvideo_dev->minor;
	(void)fe;
	VVIDEO_LOG_VERBOSE("ctx %s be_context %#x\n", ctx->name,
			   ctx->be_context);
	if (wait_event_interruptible(vl->open_wait, vvideo_link_is_on(vl))) {
		if (vl->link_on) {
			if (signal_pending(current)) {
				++vvideo_dev->stats.open_erestartsys;
				VVIDEO_LOG("ctx %s, ERESTARTSYS\n", ctx->name);
				return -ERESTARTSYS;
			}
		}
		++vvideo_dev->stats.open_eintr;
		VVIDEO_LOG("ctx %s, EINTR\n", ctx->name);
		return -EINTR;
	}
	atomic_inc(&vvideo_dev->open_count);
	VVIDEO_LOG("ctx %s, count %d\n", ctx->name,
		   atomic_read(&vvideo_dev->open_count));

	m = vvideo_link_msg_get(vl, ctx, false /*!nonblocking*/);
	if (IS_ERR(m)) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.msg_get,
			"ctx %s, failed to allocate message (%d)\n",
			   ctx->name, (int)PTR_ERR(m));
		atomic_dec(&vvideo_dev->open_count);
		return PTR_ERR(m);
	}
	VVIDEO_LOG_COMMS("ctx %s %s\n", ctx->name, m->name);
	m->req->u.open.flags = f_flags;

#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
	/*
	 * Set vvideo-fe pv version
	 */
	if (vl->fe_version == 0) {
		ver = &m->req->vvideo_ver;
		set_fe_driver_version_no_check_hdr(ver, VVIDEO_FE_VERSION);
	}
#endif

	m->req_num = VVIDEO2_REQ_OPEN;
	ret = vvideo_msg_call(m, true /*interruptible*/, true /*abortable*/);
	VVIDEO_LOG("ctx %s %s ret %d\n", ctx->name, m->name, ret);
	if (ret) {
		/* Not all errors were accounted */
		++vvideo_dev->errors.req_open;
		/*
		 * "m" can still be referenced, do not re-use it
		 * for VVIDEO2_REQ_RELEASE or anything else.
		 * (kref A) put.
		 */
		vvideo_msg_release(m, VVIDEO_MSG_THREAD);
		atomic_dec(&vvideo_dev->open_count);
		return ret;
	}

	ctx->be_context = m->req->be_context;
	snprintf(ctx->name, sizeof(ctx->name), "%d.%d.%d.%x",
		 vvideo_dev->link->number, vvideo_dev->minor, ctx->num,
		 ctx->be_context);

#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
	if (vl->be_version == 0) {
		if (!get_be_driver_version(ver)) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.get_be_version,
			"ctx %s be_version(%#x) is invalid\n", ctx->name,
				ver->be_version);
			goto open_failed_by_version_mismatch;
		}

		if (!is_fe_interface_compatible(ver, VVIDEO_FE_VERSION,
						VVIDEO_DRV_VERSION)) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.is_compatible,
			"ctx %s major version mismatching : fe(%#x), be(%#x)\n",
				ctx->name, ver->fe_version, ver->be_version);
			goto open_failed_by_version_mismatch;
		} else {
			vl->fe_version = ver->fe_version;
			vl->be_version = ver->be_version;
			VVIDEO_INFO("fe_version(%#x), be_version(%#x)\n",
				vl->fe_version, vl->be_version);
		}
	}
#endif
	/* (kref A) put */
	vvideo_msg_release(m, VVIDEO_MSG_THREAD);
	return 0;

#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
open_failed_by_version_mismatch:
	m->req_num = VVIDEO2_REQ_RELEASE;
	ret = vvideo_msg_call(m, true /*interruptible*/, true /*abortable*/);
	if (ret) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.open_release,
			"ctx %s %s RELEASE ret %d\n", ctx->name, m->name, ret);
	} else {
		VVIDEO_LOG("ctx %s %s RELEASE ok\n", ctx->name, m->name);
	}
	++vvideo_dev->errors.req_open;
	/* (kref A) put */
	vvideo_msg_release(m, VVIDEO_MSG_THREAD);
	atomic_dec(&vvideo_dev->open_count);
	return -EPERM;
#endif
}

static int vvideo_ctx_release(struct vvideo_ctx_t *ctx, _Bool nonblocking)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	int ret = 0;
	struct vvideo_msg_t *m;

	VVIDEO_LOG("ctx %s\n", ctx->name);
	m = vvideo_link_msg_get(vl, ctx, nonblocking);
	if (IS_ERR(m)) {
		if (!vl->link_on) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.msg_get,
				"ctx %s lost link getting msg\n",
				   ctx->name);
			goto out_dec;
		}
		/*
		 * Error already accounted but no error message issued.
		 * We will retry, so keep open_count unmodified.
		 */
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.msg_get,
			"ctx %s, failed to allocate message (%d)\n",
			   ctx->name, (int)PTR_ERR(m));
		return PTR_ERR(m);
	}
	m->req_num = VVIDEO2_REQ_RELEASE;
	ret = vvideo_msg_call(m, !nonblocking /*interruptible*/,
				 true /*abortable*/);
	VVIDEO_LOG("ctx %s %s ret %d\n", ctx->name, m->name, ret);
	if (ret == -ESTALE) {
		/* Retrying ESTALE requests would lead to infinite loops */
		VVIDEO_LOG("ctx %s %s, ignoring ESTALE, ret %d\n",
			   ctx->name, m->name, ret);
		ret = 0;
	} else if (!vl->link_on) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.lost_call,
			"ctx %s lost link during call\n", ctx->name);
		ret = 0;
	}
	/* (kref A) put */
	vvideo_msg_release(m, VVIDEO_MSG_THREAD);
out_dec:
	if (!ret)
		atomic_dec(&vvideo_dev->open_count);
	return ret;
}

static char *vvideo_ioctl_format(const unsigned int cmd, const void *arg,
			char *buf, size_t bufsize)
{
	switch (cmd) {
	case VIDIOC_G_CTRL:
	case VIDIOC_S_CTRL: {
		const struct v4l2_control *c = arg;

		snprintf(buf, bufsize, "%s (%#x) %#x",
			vvideo_v4l2_cid(c->id), c->id, c->value);
		break;
	}
	case VIDIOC_G_FMT:
	case VIDIOC_S_FMT: {
		const struct v4l2_format *f = arg;

		snprintf(buf, bufsize, "type %d (%s)",
			f->type, vvideo_v4l2_buf_type_short(f->type));
		break;
	}
	case VIDIOC_REQBUFS: {
		const struct v4l2_requestbuffers *rb = arg;

		snprintf(buf, bufsize, "count %d type %d (%s)",
			rb->count, rb->type,
			vvideo_v4l2_buf_type_short(rb->type));
		break;
	}
	case VIDIOC_STREAMON:
	case VIDIOC_STREAMOFF: {
		const enum v4l2_buf_type buf_type = *(const int *) arg;

		snprintf(buf, bufsize, "type %d (%s)",
			buf_type, vvideo_v4l2_buf_type_short(buf_type));
		break;
	}
	case VIDIOC_QBUF:
	case VIDIOC_DQBUF: {
		const struct v4l2_buffer *b = arg;

		snprintf(buf, bufsize, "index %d type %d (%s)",
			b->index, b->type, vvideo_v4l2_buf_type_short(b->type));
		break;
	}
	default:
		if (bufsize > 0)
			*buf = '\0';
		break;
	}
	return buf;
}

#ifdef VVIDEO_RECORD
static void vvideo_dev_record_seq(const struct vvideo_dev_t *vvideo_dev,
				struct seq_file *seq)
{
	unsigned int last = atomic_read(&vvideo_dev->next);
	unsigned int first;
	u64 prev_time = 0;

	if (last > VVIDEO_RECORD_MAX)
		first = last - VVIDEO_RECORD_MAX;
	else
		first = 0;
	for (; first < last; ++first) {
		const struct vvideo_record_t *r =
			vvideo_dev->records + first % VVIDEO_RECORD_MAX;
		const u64 time = ktime_to_ns(r->ktime) / 1000;
		const char *name = vvideo_req_split(r->req_num, r->cmd);

		seq_printf(seq, "[%4u] %lld (+%5lld): %02d %s", first,
			time, prev_time ? time - prev_time : 0, r->ctx, name);
		if (*name == '?')
			seq_printf(seq, " (%#x)", r->cmd);
		prev_time = time;
		if (r->req_num == VVIDEO2_REQ_IOCTL) {
			char buf[VVIDEO_REQ_STRING];

			seq_printf(seq, " %s",
				vvideo_ioctl_format(r->cmd, r->arg,
				buf, sizeof(buf)));
		}
		seq_puts(seq, "\n");
	}
}
#endif

#ifdef VVIDEO_ASYNC_CHECKSUMS
static unsigned long vvideo_msg_checksum(const struct vvideo_msg_t *m)
{
	const struct VVideo2Ioctl *r = &m->req->u.ioctl;
	unsigned int	arg_size = r->arg_size;
	unsigned int	ext_size = r->ext_size;
	unsigned long	check = 0;
	unsigned long	*ptr;

	check ^= r->cmd;
	check ^= arg_size;
	check ^= ext_size;
	check ^= r->arg_value;
	if (arg_size > VVIDEO2_REQ_IOCTL_ARG_SIZE)
		arg_size = VVIDEO2_REQ_IOCTL_ARG_SIZE;
	arg_size = (arg_size + sizeof(*ptr) - 1) & ~sizeof(*ptr);
	for (ptr = (unsigned long *) r->arg; arg_size >= sizeof(*ptr);
		    arg_size -= sizeof(*ptr))
		check ^= *ptr++;
	if (ext_size > VVIDEO2_REQ_IOCTL_EXT_SIZE)
		ext_size = VVIDEO2_REQ_IOCTL_EXT_SIZE;
	ext_size = (ext_size + sizeof(*ptr) - 1) & ~sizeof(*ptr);
	for (ptr = (unsigned long *) r->ext; ext_size >= sizeof(*ptr);
		    ext_size -= sizeof(*ptr))
		check ^= *ptr++;
	return check;
}
#endif

/*----- Per-VIDIOC handlers -----*/

static int vvideo_msg_querycap_after(struct vvideo_msg_t *m, int ret)
{
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_capability *c = (void *) req_ioctl->arg;

	if (ret)
		return ret;
#ifdef VVIDEO_DEBUG
	VVIDEO_LOG("ctx %s %s returned:\n",
		   ctx->name, vvideo_ioctl_name(m->ioctl.cmd));
	vvideo_v4l2_capability_cookie(vvideo_printk, 0, c);
#endif
	if (c->capabilities & V4L2_CAP_FENCES) {
		++vvideo_dev->stats.fences_ok;
#if !IS_ENABLED(CONFIG_VLX_VFENCE2)
		c->capabilities &= ~V4L2_CAP_FENCES;
		++vvideo_dev->stats.fences_removed;
#endif
	}
#ifdef V4L2_CAP_DEVICE_CAPS
	if (c->device_caps & V4L2_CAP_FENCES) {
		++vvideo_dev->stats.fences_ok;
#if !IS_ENABLED(CONFIG_VLX_VFENCE2)
		c->device_caps &= ~V4L2_CAP_FENCES;
		++vvideo_dev->stats.fences_removed;
#endif
	}
#endif
	if (ret)
		return ret;
#ifdef VVIDEO_DEVICE_CAPS
	/* vvideo_dev_t::lock is Locked here */
	if (vvideo_dev->device_caps)
		return ret;
	if (vvideo_dev->vfd->device_caps != c->device_caps) {
		VVIDEO_INFO("ctx %s adjusting device_caps %#x to %#x\n",
			ctx->name, vvideo_dev->vfd->device_caps,
			c->device_caps);
		vvideo_dev->vfd->device_caps = c->device_caps;
		vvideo_dev->device_caps = -1;
	} else {
		vvideo_dev->device_caps = 1;
	}
#endif
	return ret;
}

static const struct vvideo_call_t vvideo_call_querycap = {
	.cmd = VIDIOC_QUERYCAP,
	.after = vvideo_msg_querycap_after
};

#ifdef VVIDEO_DEBUG
static int vvideo_msg_g_fmt_after(struct vvideo_msg_t *m, int ret)
{
	const struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;

	if (ret)
		return ret;
	VVIDEO_LOG("ctx %s %s returned:\n",
		   m->ctx->name, vvideo_ioctl_name(m->ioctl.cmd));
	vvideo_v4l2_format_cookie(vvideo_printk, 0,
		(const struct v4l2_format *)req_ioctl->arg);
	return ret;
}

static const struct vvideo_call_t vvideo_call_g_fmt = {
	.cmd = VIDIOC_G_FMT,
	.after = vvideo_msg_g_fmt_after
};
#endif

#ifdef VVIDEO_S_FMT
static int vvideo_msg_s_fmt_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;

	vvideo_v4l2_format_cookie(vvideo_printk, 0,
		(const struct v4l2_format *)req_ioctl->arg);
	return 0;
}

static int vvideo_msg_s_fmt_after(struct vvideo_msg_t *m, int ret)
{
	const struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;

	if (ret)
		return ret;
	VVIDEO_INFO("ctx %s %s returned:\n", m->ctx->name,
		vvideo_ioctl_name(m->ioctl.cmd));
	vvideo_v4l2_format_cookie(vvideo_printk, 0,
		(const struct v4l2_format *) req_ioctl->arg);
	return ret;
}

static const struct vvideo_call_t vvideo_call_s_fmt = {
	.cmd = VIDIOC_S_FMT,
	.before = vvideo_msg_s_fmt_before,
	.after = vvideo_msg_s_fmt_after
};
#endif

static int vvideo_msg_reqbufs_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	const struct v4l2_requestbuffers *rb = (void *) req_ioctl->arg;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;

	if (!rb->count && vvideo_dev->reqbufs_0_ok & (1 << rb->type))
		m->async = m->ioctl.async_mask & VVIDEO_ASYNC_REQBUFS_0;
#ifdef VVIDEO_DEBUG
	vvideo_v4l2_requestbuffers_cookie(vvideo_printk, 0, rb);
#endif
	return 0;
}

static int vvideo_msg_reqbufs_after(struct vvideo_msg_t *m, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	const struct v4l2_requestbuffers *rb = (void *) req_ioctl->arg;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;

	if (ret)
		return ret;
	if (!rb->count)
		vvideo_dev->reqbufs_0_ok |= 1 << rb->type;
	return 0;
}

static const struct vvideo_call_t vvideo_call_reqbufs = {
	.cmd = VIDIOC_REQBUFS,
	.before = vvideo_msg_reqbufs_before,
	.after = vvideo_msg_reqbufs_after
};

/* vvideo_dev_t::lock is Locked here */

static int vvideo_msg_buffer_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_buffer *b = (void *)req_ioctl->arg;
	const unsigned int b_length = b->length;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	size_t planes_size;
	struct vvideo_dmabuf_gid_t *gids;
	int plane;
	unsigned int flags;
	int ret = 0;

	/* For VIDIOC_QUERYBUF, b->memory is not set on entry */
	if (b->memory != V4L2_MEMORY_DMABUF &&
	    m->ioctl.cmd != VIDIOC_QUERYBUF) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.not_dmabuf,
			"ctx %s %s (%d) not supported, cmd %#x (%s)\n",
			m->ctx->name, vvideo_v4l2_memory(b->memory),
			b->memory, m->ioctl.cmd,
			vvideo_ioctl_name(m->ioctl.cmd));
#ifdef VVIDEO_DEBUG
		vvideo_v4l2_buffer_cookie(vvideo_printk, 0, b);
#endif
		return -EINVAL;
	}
	(void)vl;
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	m->ioctl.vfid = VFENCE2_ID_NONE;
	if (b->reserved >= 0 && b->flags & V4L2_BUF_FLAG_IN_FENCE) {
		++vvideo_dev->stats.ioctl.in_fence;
		ret = vfence2_export_fence(vl->vfence2_connection, b->reserved,
					   &m->ioctl.vfid, 1 /*can_block*/);
		if (ret) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.export_fence,
		    "ctx %s cmd %#x (%s) vfence2_export_fence failed (%d)\n",
				m->ctx->name, m->ioctl.cmd,
				vvideo_ioctl_name(m->ioctl.cmd), ret);
			return ret;
		}
		if (m->ioctl.vfid == VFENCE2_ID_NONE) {
			b->flags &= ~V4L2_BUF_FLAG_IN_FENCE;
			b->reserved = 0;
		} else {
			b->reserved = m->ioctl.vfid;
		}
	}
#endif
	if (!V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		++vvideo_dev->stats.ioctl.single_planar;
		return 0;
	}
	m->ioctl.orig_user_ptr = (void __user *)b->m.planes;
	if (!b_length)
		return 0;
	planes_size = sizeof(struct v4l2_plane) * b_length;
	gids = (struct vvideo_dmabuf_gid_t *) (req_ioctl->ext + planes_size);

	req_ioctl->ext_size = (sizeof(struct v4l2_plane) +
		sizeof(struct vvideo_dmabuf_gid_t)) * b_length;
	if (b_length > VVIDEO2_REQ_IOCTL_EXT_SIZE /
	    (sizeof(struct v4l2_plane) + sizeof(struct vvideo_dmabuf_gid_t))) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.ext_size,
		    "ctx %s ioctl ext ctrl too large, size %u, cmd %#x (%s)\n",
			m->ctx->name, req_ioctl->ext_size,
			m->ioctl.cmd, vvideo_ioctl_name(m->ioctl.cmd));
		ret = -EINVAL;
		goto error;
	}
	/* The v4l2_buffer struct fits in arg[] so ext[] is still empty */
	if (copy_from_user(req_ioctl->ext, m->ioctl.orig_user_ptr,
			   planes_size)) {
		++vvideo_dev->errors.copy_from_user;
		ret = -EFAULT;
		goto error;
	}
	b->m.planes = (struct v4l2_plane *)req_ioctl->ext;
	switch (m->ioctl.cmd) {
	case VIDIOC_DQBUF:
	default:
		return 0;
	case VIDIOC_QBUF:
		flags = VVIDEO_DMABUF_QUEUED | VVIDEO_DMABUF_RUNNING;
		break;
	case VIDIOC_PREPARE_BUF:
		flags = VVIDEO_DMABUF_PREPARED | VVIDEO_DMABUF_RUNNING;
		break;
	}
#ifdef VVIDEO_DEBUG
	VVIDEO_LOG("ctx %s Buffer before fd2gid for %s:\n",
		   m->ctx->name, vvideo_ioctl_name(m->ioctl.cmd));
	vvideo_v4l2_buffer_cookie(vvideo_printk, 0, b);
#endif
	for (plane = 0; plane < b_length; ++plane) {
		const int fd = b->m.planes[plane].m.fd;

		ret = vvideo_dmabuf_fd2gid(m->ctx, fd, b->index, plane,
					b->type, gids + plane, flags);
		if (ret) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.fd2gid,
			    "ctx %s vvideo_dmabuf_fd2gid (%d) failed (%d)\n",
				m->ctx->name, fd, ret);
			while (plane-- > 0)
				vvideo_dmabuf_release_gid(m->ctx, gids + plane,
					flags);
			goto error;
		}
	}
	return 0;

error:
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (m->ioctl.vfid != VFENCE2_ID_NONE) {
		const int ret2 = vfence2_unexport_fence(
			vvideo_dev->link->vfence2_connection, m->ioctl.vfid);
		if (ret2)
			VVIDEO_WARN_LIMITED(vvideo_dev->warnings.unexport_fence,
				"vfence2_unexport_fence %#x failed (%d)\n",
				m->ioctl.vfid, ret2);
	}
#endif
	return ret;
}

/* vvideo_dev_t::lock is Locked here */

static int vvideo_msg_buffer_after(struct vvideo_msg_t *m, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_buffer *b = (void *)req_ioctl->arg;
	const unsigned int b_length = b->length;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	unsigned int ext_size;

	if (ret < 0) {
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		/*
		 * We cannot unexport immediately all the time, to avoid
		 * leaks in case backend forgot to import during the
		 * synchronous call, because this signals the fence.
		 * So only unexport on error.
		 */
		if (m->ioctl.vfid != VFENCE2_ID_NONE) {
			const int ret2 = vfence2_unexport_fence(
				vvideo_dev->link->vfence2_connection,
				m->ioctl.vfid);
			if (ret2)
				VVIDEO_WARN_LIMITED(
					vvideo_dev->warnings.unexport_fence,
				"vfence2_unexport_fence %#x failed (%d)\n",
					m->ioctl.vfid, ret2);
			m->ioctl.vfid = VFENCE2_ID_NONE;
		}
#endif
		if (m->ioctl.cmd != VIDIOC_QBUF &&
		    m->ioctl.cmd != VIDIOC_PREPARE_BUF)
			return ret;
	}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (!ret && b->reserved != VFENCE2_ID_NONE &&
	    b->flags & V4L2_BUF_FLAG_OUT_FENCE) {
		int fence_fd;

		++vvideo_dev->stats.ioctl.out_fence;
		ret = vfence2_import_fence(vvideo_dev->link->vfence2_connection,
			b->reserved, &fence_fd);
		if (ret) {
			VVIDEO_WARN_LIMITED(vvideo_dev->warnings.import_fence,
		    "ctx %s cmd %#x (%s) vfence2_import_fence failed (%d)\n",
				ctx->name, m->ioctl.cmd,
				vvideo_ioctl_name(m->ioctl.cmd), ret);
			ret = 0;
			fence_fd = -1;
			b->flags &= ~V4L2_BUF_FLAG_OUT_FENCE;
		}
		b->reserved = fence_fd;
	}
#endif
	if (!V4L2_TYPE_IS_MULTIPLANAR(b->type))
		return ret;
	/*
	 * b->m.planes was redirected to PMEM, but m->ioctl.orig_user_ptr
	 * saves the original user mode address.
	 * We must restore b->m.planes because the structure
	 * is copied back out to user space.
	 * UPDATE: Do it below, we still need the local copy.
	 */
	if (!b_length) {
		/* Redirect back to user space */
		b->m.planes = (void *)m->ioctl.orig_user_ptr;
		return ret;
	}
	/* Convert GIDs back to FDs */
	/* QUERYBUF does not return FDs */
	if (m->ioctl.cmd != VIDIOC_QUERYBUF) {
		const size_t planes_size = sizeof(struct v4l2_plane) * b_length;
		struct vvideo_dmabuf_gid_t *const gids =
			(struct vvideo_dmabuf_gid_t
				 *)(req_ioctl->ext + planes_size);
		int plane;
		unsigned int flags;
		_Bool update_fd;

		if (m->ioctl.cmd == VIDIOC_QBUF) {
			/* Leave original fd on gid2fd error */
			update_fd = false;
			flags = VVIDEO_DMABUF_RUNNING;
			if (ret)
				flags |= VVIDEO_DMABUF_QUEUED;
		} else if (m->ioctl.cmd == VIDIOC_DQBUF) {
			update_fd = true;
			flags = VVIDEO_DMABUF_QUEUED;
		} else { /* VIDIOC_PREPARE_BUF */
			/* Leave original fd on gid2fd error */
			update_fd = false;
			flags = VVIDEO_DMABUF_RUNNING;
			if (ret)
				flags |= VVIDEO_DMABUF_PREPARED;
		}
#ifdef VVIDEO_DEBUG
		VVIDEO_LOG("ctx %s Buffer before gid2fd:\n", ctx->name);
		vvideo_v4l2_buffer_cookie(vvideo_printk, 0, b);
#endif
		for (plane = 0; plane < b_length; ++plane) {
			const int fd = vvideo_dmabuf_gid2fd(ctx,
				gids + plane, flags, m->ioctl.cmd);

			/* If not update_fd, leave original fd on error */
			if (VVIDEO_FD_VALID(fd) || update_fd)
				b->m.planes[plane].m.fd = fd;
		}
#ifdef VVIDEO_DEBUG
		VVIDEO_LOG("ctx %s Buffer after gid2fd:\n", ctx->name);
		vvideo_v4l2_buffer_cookie(vvideo_printk, 0, b);
#endif
	}
	ext_size = sizeof(struct v4l2_plane) * b_length;
	if (b_length > VVIDEO2_REQ_IOCTL_EXT_SIZE /
	    sizeof(struct v4l2_plane)) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.ext_size,
		    "ctx %s ioctl ext ctrl too large, size %u, cmd %#x (%s)\n",
			ctx->name, ext_size,
			m->ioctl.cmd, vvideo_ioctl_name(m->ioctl.cmd));
		ret = -EINVAL;
		goto error;
	}
	if (copy_to_user(m->ioctl.orig_user_ptr, req_ioctl->ext, ext_size)) {
		++vvideo_dev->errors.copy_to_user;
		ret = -EFAULT;
		goto error;
	}
	/* Redirect back to user space */
	b->m.planes = (void *)m->ioctl.orig_user_ptr;
	return ret;

error:
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (m->ioctl.vfid != VFENCE2_ID_NONE) {
		const int ret2 = vfence2_unexport_fence(
			vvideo_dev->link->vfence2_connection, m->ioctl.vfid);
		if (ret2)
			VVIDEO_WARN_LIMITED(vvideo_dev->warnings.unexport_fence,
				"vfence2_unexport_fence %#x failed (%d)\n",
				m->ioctl.vfid, ret2);
	}
#endif
	return ret;
}

static const struct vvideo_call_t vvideo_call_querybuf = {
	.cmd = VIDIOC_QUERYBUF,
	.before = vvideo_msg_buffer_before,
	.after = vvideo_msg_buffer_after
};

static const struct vvideo_call_t vvideo_call_qbuf = {
	.cmd = VIDIOC_QBUF,
	.before = vvideo_msg_buffer_before,
	.after = vvideo_msg_buffer_after
};

/* vvideo_dev_t::lock is Locked here */

static int vvideo_msg_dqbuf_before(struct vvideo_msg_t *m)
{
	const int ret = vvideo_msg_buffer_before(m);

	if (ret)
		return ret;
	/* vvideo_dev_t::lock is Locked here, no need for atomics */
	++m->ctx->running_dqbufs;
	return 0;
}

#ifdef VVIDEO_DPBS
static int vvideo_msg_dpbs_import(struct vvideo_msg_t *m, int *fd)
{
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_dmabuf_gid_t gid;
	int ret;

	VVIDEO_ZERO(&gid);
	gid.gid = *fd;
	ret = vvideo_dmabuf_gid2fd(ctx, &gid, VVIDEO_DMABUF_DELETED,
		m->ioctl.cmd);
	if (ret < 0) {
		/* Error message already issued */
		++vvideo_dev->errors.dpbs.import;
		return ret;
	}
	++ctx->dev->stats.dpbs.import;
	VVIDEO_LOG("ctx %s gid %#x fd %d\n", ctx->name, gid.gid, ret);
	*fd = ret;
	return 0;
}

static void vvideo_msg_dpbs_dqbuf_after(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct v4l2_buffer *b = (void *)req_ioctl->arg;
	struct mfc_dec_dpb_ref_info *info;
	int buffer;

	if (b->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return;
	if (!ctx->dpbs.info)
		return;
	if (b->index >= MFC_MAX_BUFFERS) {
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.dpbs.index,
			"ctx %s b->index %d too high\n", ctx->name, b->index);
		return;
	}
	/* Shared memory could be too small */
	if (b->index >= ctx->dpbs.dma_buf->size / sizeof(*info)) {
		VVIDEO_WARN_LIMITED(ctx->dev->warnings.dpbs.size,
			"ctx %s b->index %d too high for size %#lx\n",
			ctx->name, b->index, ctx->dpbs.dma_buf->size);
		return;
	}
	info = ctx->dpbs.info + b->index;
	VVIDEO_LOG("ctx %s index %d/%d\n", ctx->name, b->index, info->index);
	for (buffer = 0; buffer < MFC_MAX_BUFFERS; ++buffer) {
		struct mfc_stored_dpb_info *dpb = &info->dpb[buffer];
		int plane;

		if (dpb->fd[0] < 0)
			break; /* End of buffers */
		if (dpb->fd[0] > 0)
			VVIDEO_LOG("slot %d: %d %d %d\n",
				buffer, dpb->fd[0], dpb->fd[1], dpb->fd[2]);
		for (plane = 0; plane < MFC_MAX_PLANES; ++plane) {
			if (dpb->fd[plane] < 0)
				break; /* End of planes */
			if (dpb->fd[plane] == VDMAHEAP_GID_INVALID) {
				dpb->fd[plane] = -1;
				/*
				 * We must break, otherwise application
				 * would never free file descriptors following
				 * a negative one, as it means end-of-list.
				 */
				break;
			}
			if (vvideo_msg_dpbs_import(m, dpb->fd + plane)) {
				dpb->fd[plane] = -1;
				/* Same remark as above */
				break;
			}
		}
	}
}
#endif

/* vvideo_dev_t::lock is Locked here */

static int vvideo_msg_dqbuf_after(struct vvideo_msg_t *m, int ret)
{
	struct vvideo_ctx_t *ctx = m->ctx;

	ret = vvideo_msg_buffer_after(m, ret);
#ifdef VVIDEO_DPBS
	if (!ret)
		vvideo_msg_dpbs_dqbuf_after(m);
#endif
	/* vvideo_dev_t::lock is Locked here, no need for atomics */
	if (--ctx->running_dqbufs > 0)
		return ret;
	ctx->running_dqbufs = 0;
	if (!ctx->streamoff_pending)
		return ret;
	VVIDEO_LOG_DMABUF("ctx %s streamoff_pending %#x\n",
		ctx->name, ctx->streamoff_pending);
	do {
		const int bit = __fls(ctx->streamoff_pending);

		vvideo_dmabuf_release_buf_type(ctx, bit);
		ctx->streamoff_pending &= ~(1 << bit);
	} while (ctx->streamoff_pending);
	return ret;
}

static const struct vvideo_call_t vvideo_call_dqbuf = {
	.cmd = VIDIOC_DQBUF,
	.before = vvideo_msg_dqbuf_before,
	.after = vvideo_msg_dqbuf_after
};

static const struct vvideo_call_t vvideo_call_prepare_buf = {
	.cmd = VIDIOC_PREPARE_BUF,
	.before = vvideo_msg_buffer_before,
	.after = vvideo_msg_buffer_after
};

/* vvideo_dev_t::lock is Locked here */

static int vvideo_msg_expbuf_after(struct vvideo_msg_t *m, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_v4l2_exportbuffer *eb = (void *)req_ioctl->arg;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	char buf[VVIDEO_GID_STRING];
	struct dma_buf *dma_buf;

	if (ret)
		return ret;
	ret = vvideo_dmabuf_import(ctx, &eb->gid, &dma_buf);
	if (ret)	/* Error message already issued */
		return ret;
	/* dma_buf_fd() alone does not increase the f_count */
	ret = dma_buf_fd(dma_buf, O_CLOEXEC | O_RDWR);
	if (ret < 0) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dma_buf_fd,
			"ctx %s dma_buf_fd(%p) failed (%d)\n",
			ctx->name, dma_buf, ret);
		/* Warning already accounted and logged if failure */
		vvideo_dmabuf_unexport(ctx, &eb->gid);
		return ret;
	}
	eb->eb.fd = ret;
	(void) buf;
	VVIDEO_LOG("ctx %s gid %s fd %d\n",
		ctx->name, vvideo_dmabuf_gid_format(&eb->gid, buf,
		sizeof(buf)), eb->eb.fd);
	return 0;
}

static const struct vvideo_call_t vvideo_call_expbuf = {
	.cmd = VIDIOC_EXPBUF,
	.after = vvideo_msg_expbuf_after
};

static int vvideo_msg_stream_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	const enum v4l2_buf_type buf_type = *(const int *)req_ioctl->arg;

	VVIDEO_LOG(" type: %d (%s)\n", buf_type,
		   vvideo_v4l2_buf_type_short(buf_type));
	if (m->ioctl.cmd == VIDIOC_STREAMON &&
	    m->ctx->dev->streamon_ok & (1 << buf_type))
		m->async = m->ioctl.async_mask & VVIDEO_ASYNC_STREAMON;
	return 0;
}

static int vvideo_msg_streamon_after(struct vvideo_msg_t *m, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	const enum v4l2_buf_type buf_type = *(const int *)req_ioctl->arg;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;

	if (ret)
		return ret;
	vvideo_dev->streamon_ok |= 1 << buf_type;
	return 0;
}

static const struct vvideo_call_t vvideo_call_streamon = {
	.cmd = VIDIOC_STREAMON,
	.before = vvideo_msg_stream_before,
	.after = vvideo_msg_streamon_after
};

/* vvideo_dev_t::lock is Locked here */

static int vvideo_msg_streamoff_after(struct vvideo_msg_t *m, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_ctx_t *ctx = m->ctx;
	const enum v4l2_buf_type buf_type = *(const int *)req_ioctl->arg;

	if (ret)
		return ret;
	if (ctx->running_dqbufs > 0) {
		++ctx->dev->stats.streamoff_pending;
		ctx->streamoff_pending = 1 << buf_type;
		VVIDEO_LOG_DMABUF("ctx %s streamoff_pending %#x\n", ctx->name,
			  ctx->streamoff_pending);
	} else {
		vvideo_dmabuf_release_buf_type(ctx, buf_type);
	}
	return 0;
}

static const struct vvideo_call_t vvideo_call_streamoff = {
	.cmd = VIDIOC_STREAMOFF,
	.before = vvideo_msg_stream_before,
	.after = vvideo_msg_streamoff_after
};

static _Bool vvideo_known_cid(unsigned int cid)
{
	if (vvideo_cid_user_shared_handle(cid))
		return true;
	switch (cid) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
	case V4L2_CID_MPEG_MFC51_VIDEO_FRAME_TAG:
	case V4L2_CID_MPEG_MFC51_VIDEO_CHECK_STATE:
	case V4L2_CID_MPEG_MFC51_VIDEO_DISPLAY_STATUS:
	case V4L2_CID_MPEG_MFC51_VIDEO_FRAME_POC:
	case V4L2_CID_MPEG_VIDEO_SRC_BUF_FLAG:
	case V4L2_CID_MPEG_VIDEO_DST_BUF_FLAG:
	case V4L2_CID_MPEG_VIDEO_GDC_VOTF:
	case V4L2_CID_MPEG_VIDEO_FRAME_ERROR_TYPE:
	case V4L2_CID_MPEG_VIDEO_GOP_CTRL:
	case V4L2_CID_MPEG_VIDEO_GET_DISPLAY_DELAY:
	case V4L2_CID_MPEG_MFC51_VIDEO_FRAME_RATE:
	case V4L2_CID_MPEG_VIDEO_QOS_RATIO:
	case V4L2_CID_MPEG_MFC_GET_VERSION_INFO:
	case V4L2_CID_MPEG_MFC_GET_EXTRA_BUFFER_SIZE:
	case V4L2_CID_MPEG_MFC_SET_DYNAMIC_DPB_MODE:
	case V4L2_CID_MPEG_MFC_GET_EXT_INFO:
	case V4L2_CID_MPEG_MFC_GET_10BIT_INFO:
	case V4L2_CID_MPEG_MFC_GET_DRIVER_INFO:
	case V4L2_CID_MPEG_VIDEO_HEVC_PREPEND_SPSPPS_TO_IDR:
	case V4L2_CID_MPEG_VIDEO_SKIP_LAZY_UNMAP:
	case V4L2_CID_MPEG_VIDEO_HIERARCHICAL_BITRATE_CTRL:
	case V4L2_CID_MPEG_VIDEO_DECODING_ORDER:
		break;
#ifdef SC_CID_FRAMERATE
	case SC_CID_FRAMERATE:
		break;
#endif
#ifdef SC_CID_CTX_PRIORITY_SETTING
	case SC_CID_CTX_PRIORITY_SETTING:
		break;
#endif
	case V4L2_CID_ROTATE:
	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		break;
    /* sadk9-sys-linux/include/uapi/linux/videodev2_exynos_media.h */
#ifdef V4L2_CID_CACHEABLE
	case V4L2_CID_CACHEABLE:
		break;
#endif
#ifdef V4L2_CID_CSC_EQ_MODE
	case V4L2_CID_CSC_EQ_MODE:
		break;
#endif
#ifdef V4L2_CID_CSC_EQ
	case V4L2_CID_CSC_EQ:
		break;
#endif
#ifdef V4L2_CID_CSC_RANGE
	case V4L2_CID_CSC_RANGE:
		break;
#endif
#ifdef V4L2_CID_CONTENT_PROTECTION
	case V4L2_CID_CONTENT_PROTECTION:
		break;
#endif
#ifdef V4L2_SC_CID_SRC_CSPAN
	case V4L2_SC_CID_SRC_CSPAN:
		break;
#endif
#ifdef V4L2_SC_CID_DST_CSPAN
	case V4L2_SC_CID_DST_CSPAN:
		break;
#endif
	default:
		return false;
	}
	return true;
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

static int vvideo_msg_dpbs_s_ctrl_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct v4l2_control *c = (void *)req_ioctl->arg;
	int ret;

	if (c->id != V4L2_CID_MPEG_MFC_SET_USER_SHARED_HANDLE)
		return 0;
	if (ctx->dpbs.dma_buf) {
		/* Only allow it once */
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dpbs.multiple,
			"ctx %s already have handle %d\n",
			ctx->name, ctx->dpbs.fd);
		return -EBUSY;
	}
	ctx->dpbs.fd = c->value;
	ctx->dpbs.dma_buf = dma_buf_get(ctx->dpbs.fd); /* Increases f_count */
	if (IS_ERR(ctx->dpbs.dma_buf)) {
		ret = PTR_ERR(ctx->dpbs.dma_buf);
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.dpbs.get,
			"ctx %s dma_buf_get(%d) failed (%d)\n",
			ctx->name, ctx->dpbs.fd, ret);
		ctx->dpbs.dma_buf = NULL;
		vvideo_ctx_dpbs_release(ctx); /* For dpbs.fd only */
		return ret;
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
		return -ENOMEM;
	}
	++vvideo_dev->stats.dpbs.s_ctrl;
	return 0;
}
#endif

static int vvideo_msg_g_ctrl_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_control *c = (void *)req_ioctl->arg;
	const unsigned int cid = c->id;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;

	VVIDEO_LOG("ctx %s %s: %s\n", m->ctx->name,
		vvideo_ioctl_name(m->ioctl.cmd), vvideo_v4l2_cid(cid));
	if (!vvideo_known_cid(cid))
		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.unknown_cid,
			"ctx %s Unknown CID %#x %s\n",
			m->ctx->name, cid, vvideo_v4l2_cid(cid));
	return 0;
}

static int vvideo_msg_s_ctrl_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_control *c = (void *)req_ioctl->arg;
	const unsigned int cid = c->id;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;
	char buf[VVIDEO_GID_STRING];
	int ret;

	m->async = m->ioctl.async_mask & VVIDEO_ASYNC_S_CTRL;
	VVIDEO_LOG("ctx %s %s: %s: %d\n",
		m->ctx->name, vvideo_ioctl_name(m->ioctl.cmd),
		vvideo_v4l2_cid(cid), c->value);
	if (!vvideo_known_cid(cid)) {
		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.unknown_cid,
			"ctx %s Unknown CID %#x %s\n",
			m->ctx->name, cid, vvideo_v4l2_cid(cid));
		return 0;
	}
	if (!vvideo_cid_user_shared_handle(cid))
		return 0;
#ifdef VVIDEO_DPBS
	ret = vvideo_msg_dpbs_s_ctrl_before(m);
	if (ret)
		return ret;
#endif
	/*
	 * buf_type 0 is not valid, so QUEUED is not going to be
	 * removed by a STREAMOFF.
	 */
	ret = vvideo_dmabuf_fd2gid(m->ctx, c->value, -1 /*buf_index*/,
		0 /*plane*/, 0 /*buf_type*/, VVIDEO_CTRL_TO_GID(c),
		VVIDEO_DMABUF_RUNNING | VVIDEO_DMABUF_QUEUED);
	if (ret) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.fd2gid,
			"ctx %s fd2gid(%d) failed for %#x (%s) (%d)\n",
			m->ctx->name, c->value, cid,
			vvideo_v4l2_cid(cid), ret);
#ifdef VVIDEO_DPBS
		vvideo_ctx_dpbs_release(m->ctx);
#endif
		return ret;
	}
	(void) buf;
	VVIDEO_LOG("ctx %s Mapped fd %d to gid %s in %#x (%s)\n",
		m->ctx->name, c->value,
		vvideo_dmabuf_gid_format(VVIDEO_CTRL_TO_GID(c),
		buf, sizeof(buf)), cid, vvideo_v4l2_cid(cid));
	return 0;
}

#ifdef VVIDEO_DPBS
static void vvideo_msg_dpbs_s_ctrl_after(struct vvideo_msg_t *m, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct v4l2_control *c = (void *)req_ioctl->arg;

	if (c->id != V4L2_CID_MPEG_MFC_SET_USER_SHARED_HANDLE)
		return;
	if (!ret)
		return;
	if (!ctx->dpbs.dma_buf)
		return;
	vvideo_ctx_dpbs_release(ctx);
}
#endif

static int vvideo_msg_ctrl_after(struct vvideo_msg_t *m, int ret)
{
	struct vvideo_ctx_t *ctx = m->ctx;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct v4l2_control *c = (void *)req_ioctl->arg;
	char buf[VVIDEO_GID_STRING];
	unsigned int flags;

	if (!ret && m->ioctl.cmd == VIDIOC_G_CTRL)
		VVIDEO_LOG("ctx %s %s cid %#x (%s) gave %d\n",
			ctx->name, vvideo_ioctl_name(m->ioctl.cmd),
			c->id, vvideo_v4l2_cid(c->id), c->value);
	if (!vvideo_cid_user_shared_handle(c->id))
		return ret;
	/* Convert GID back to FD */
	if (m->ioctl.cmd == VIDIOC_S_CTRL) {
		++ctx->dev->stats.s_gid;
#ifdef VVIDEO_DPBS
		vvideo_msg_dpbs_s_ctrl_after(m, ret);
#endif
		flags = VVIDEO_DMABUF_RUNNING;
		if (ret)
			flags |= VVIDEO_DMABUF_QUEUED;
	} else {
		++ctx->dev->stats.g_gid;
		if (ret)
			return ret;
		flags = 0;
	}
	c->value = vvideo_dmabuf_gid2fd(ctx, VVIDEO_CTRL_TO_GID(c), flags,
		m->ioctl.cmd);
	(void) buf;
	VVIDEO_LOG("ctx %s %s cid %#x (%s) gid %s value %d\n",
		ctx->name, vvideo_ioctl_name(m->ioctl.cmd),
		c->id, vvideo_v4l2_cid(c->id),
		vvideo_dmabuf_gid_format(VVIDEO_CTRL_TO_GID(c), buf,
		sizeof(buf)), c->value);
	return ret;
}

static const struct vvideo_call_t vvideo_call_g_ctrl = {
	.cmd = VIDIOC_G_CTRL,
	.before = vvideo_msg_g_ctrl_before,
	.after = vvideo_msg_ctrl_after
};

static const struct vvideo_call_t vvideo_call_s_ctrl = {
	.cmd = VIDIOC_S_CTRL,
	.before = vvideo_msg_s_ctrl_before,
	.after = vvideo_msg_ctrl_after
};

#define VVIDEO_EDID_SIZE	128

static int vvideo_msg_edid_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;
	struct v4l2_edid *edid = (void *)req_ioctl->arg;
	u8 *const req_blocks = (void *) (edid + 1);
	const unsigned int edid_blocks = edid->blocks;	/* Read once */

	/*
	 * To gain on PMEM space, VVIDEO_EDID_SIZE byte blocks follow
	 * struct v4l2_edid immediately at req_blocks.
	 * First limit is API, second comes from PMEM buffer size.
	 */
	if (edid_blocks > 256 || edid_blocks > (VVIDEO2_REQ_IOCTL_TOTAL_SIZE -
	    sizeof(struct v4l2_edid)) / VVIDEO_EDID_SIZE) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.edid_blocks,
			"ctx %s too many EDID blocks %u\n",
			m->ctx->name, edid_blocks);
		return -E2BIG;
	}
	m->ioctl.orig_user_ptr = (void __user *) edid->edid;
	if (m->ioctl.cmd == VIDIOC_S_EDID && edid_blocks &&
	    copy_from_user(req_blocks, m->ioctl.orig_user_ptr,
			   edid_blocks * VVIDEO_EDID_SIZE)) {
		++vvideo_dev->errors.copy_from_user;
		return -EFAULT;
	}
	edid->edid = req_blocks;
	return 0;
}

static int vvideo_msg_edid_after(struct vvideo_msg_t *m, int ret)
{
	const struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_dev_t *vvideo_dev = m->ctx->dev;
	struct v4l2_edid *edid = (void *)req_ioctl->arg;
	u8 *const req_blocks = (void *) (edid + 1);
	const unsigned int edid_blocks = edid->blocks;	/* Read once */

	if (ret)
		return ret;
	if (edid_blocks > (VVIDEO2_REQ_IOCTL_TOTAL_SIZE -
	    sizeof(struct v4l2_edid)) / VVIDEO_EDID_SIZE) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.edid_blocks,
			"ctx %s too many EDID blocks %u\n",
			m->ctx->name, edid_blocks);
		return -E2BIG;
	}
	if (m->ioctl.cmd == VIDIOC_G_EDID && edid_blocks &&
	    copy_to_user(m->ioctl.orig_user_ptr, req_blocks,
			 edid_blocks * VVIDEO_EDID_SIZE)) {
		++vvideo_dev->errors.copy_to_user;
		return -EFAULT;
	}
	edid->edid = m->ioctl.orig_user_ptr;
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

#ifdef VVIDEO_DEBUG
static int vvideo_msg_g_crop_after(struct vvideo_msg_t *m, int ret)
{
	const struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;

	if (ret)
		return ret;
	VVIDEO_LOG("ctx %s %s returned:\n",
		   m->ctx->name, vvideo_ioctl_name(m->ioctl.cmd));
	vvideo_v4l2_crop_cookie(vvideo_printk, 0,
		(const struct v4l2_crop *)req_ioctl->arg);
	return ret;
}

static const struct vvideo_call_t vvideo_call_g_crop = {
	.cmd = VIDIOC_G_CROP,
	.after = vvideo_msg_g_crop_after
};
#endif

static int vvideo_msg_s_crop_before(struct vvideo_msg_t *m)
{
	m->async = m->ioctl.async_mask & VVIDEO_ASYNC_S_CROP;
	return 0;
}

static const struct vvideo_call_t vvideo_call_s_crop = {
	.cmd = VIDIOC_S_CROP,
	.before = vvideo_msg_s_crop_before
};

#define VVIDEO2_EXT_CTRLS_MAX \
	(VVIDEO2_REQ_IOCTL_EXT_SIZE / sizeof(struct v4l2_ext_control))

static int vvideo_msg_ext_ctrls_before(struct vvideo_msg_t *m)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct v4l2_ext_controls *p = (void *)req_ioctl->arg;
	const unsigned int p_count = p->count; /* Take snapshot */
	struct v4l2_ext_control *controls; /* req_ioctl->ext */
	struct vvideo_dmabuf_gid_t *gids; /* at end of req_ioctl->ext */
	unsigned int flags;
	unsigned int max_gid;
	unsigned int ext_size;
	unsigned int i;
	unsigned int gid;
	int ret;

	VVIDEO_LOG("ctx %s %s count %d\n", ctx->name,
		vvideo_ioctl_name(m->ioctl.cmd), p_count);
	p->error_idx = p_count;
	m->ioctl.orig_user_ptr = (void __user *)p->controls;
	if (!p_count)
		return 0;
	req_ioctl->ext_size = ext_size =
		sizeof(struct v4l2_ext_control) * p_count;

	if (p_count > VVIDEO2_EXT_CTRLS_MAX) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.ext_size,
			"ctx %s %s size %u too large\n", ctx->name,
			vvideo_ioctl_name(m->ioctl.cmd), ext_size);
		return -EINVAL;
	}
	/* The v4l2_ext_controls struct fits in arg[] so ext[] is still empty */
	if (copy_from_user(req_ioctl->ext, m->ioctl.orig_user_ptr, ext_size)) {
		++vvideo_dev->errors.copy_from_user;
		return -EFAULT;
	}
	p->controls = controls = (void *)req_ioctl->ext;
	/* Process selected CIDs and convert FDs to GIDs */
	switch (m->ioctl.cmd) {
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
	gids = (void *)(controls + p_count);
	max_gid = (sizeof(req_ioctl->ext) - p_count * sizeof(*controls)) /
		sizeof(*gids);
	for (i = 0, gid = 0; i < p_count; ++i) {
		const struct v4l2_ext_control *c = controls + i;
		char buf[VVIDEO_GID_STRING];

		if (!vvideo_cid_user_shared_handle(c->id))
			continue;
		if (gid >= max_gid) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.ext_gid,
				"ctx %s cid %#x (%s) too many gids (%d))\n",
				ctx->name, c->id, vvideo_v4l2_cid(c->id), gid);
			ret = -E2BIG;
			goto exit_undo;
		}
		/*
		 * buf_type 0 is not valid, so QUEUED is not going to be
		 * removed by a STREAMOFF.
		 */
		ret = vvideo_dmabuf_fd2gid(ctx, c->value, -1 /*buf_index*/,
			0 /*plane*/, 0 /*buf_type*/, gids + gid, flags);
		if (ret) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.fd2gid,
				"ctx %s fd2gid(%d) failed for %#x (%s) (%d)\n",
				ctx->name, c->value, c->id,
				vvideo_v4l2_cid(c->id), ret);
			goto exit_undo;
		}
		(void) buf;
		VVIDEO_LOG("ctx %s Mapped fd %d to gid %s in %#x (%s)\n",
			ctx->name, c->value, vvideo_dmabuf_gid_format(gids
			+ gid, buf, sizeof(buf)), c->id,
			vvideo_v4l2_cid(c->id));
		++gid;
	}
	return 0;

exit_undo:
	while (gid-- > 0)
		vvideo_dmabuf_release_gid(ctx, gids + gid, flags);
	return ret;
}

static int vvideo_msg_ext_ctrls_after(struct vvideo_msg_t *m, int ret)
{
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct v4l2_ext_controls *p = (void *)req_ioctl->arg;
	const unsigned int p_count = p->count; /* Take snapshot */
	struct v4l2_ext_control *controls;
	const struct vvideo_dmabuf_gid_t *gids;
	unsigned int ext_size;
	unsigned int flags;
	_Bool update_fd;
	unsigned int i;
	unsigned int max_gid;
	unsigned int gid;

	VVIDEO_LOG("ctx %s %s count %d ret %d\n", ctx->name,
		vvideo_ioctl_name(m->ioctl.cmd), p_count, ret);
	p->controls = (void *)m->ioctl.orig_user_ptr;
	/* Convert GIDs back to FDs */
	switch (m->ioctl.cmd) {
	case VIDIOC_G_EXT_CTRLS:
	default:
		if (ret)
			return ret;
		update_fd = true;
		flags = 0;
		break;
	case VIDIOC_S_EXT_CTRLS:
		/* Leave original fd on gid2fd error */
		update_fd = false;
		flags = VVIDEO_DMABUF_RUNNING;
		if (ret)
			flags |= VVIDEO_DMABUF_QUEUED;
		break;
	case VIDIOC_TRY_EXT_CTRLS:
		/* Leave original fd on gid2fd error */
		update_fd = false;
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
		int fd;

		if (!vvideo_cid_user_shared_handle(c->id))
			continue;
		if (m->ioctl.cmd == VIDIOC_G_EXT_CTRLS)
			++vvideo_dev->stats.g_ext_gid;
		else if (m->ioctl.cmd == VIDIOC_G_EXT_CTRLS)
			++vvideo_dev->stats.s_ext_gid;
		else
			++vvideo_dev->stats.try_ext_gid;
		if (gid >= max_gid) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.ext_gid,
				"ctx %s cid %#x (%s) too many gids (%d))\n",
				ctx->name, c->id, vvideo_v4l2_cid(c->id), gid);
			return -EINVAL;
		}
		fd = vvideo_dmabuf_gid2fd(ctx, gids + gid, flags, m->ioctl.cmd);
		/* If not update_fd, leave original fd on error */
		if (VVIDEO_FD_VALID(fd) || update_fd)
			c->value = fd;
		(void) buf;
		VVIDEO_LOG("ctx %s %s cid #%x (%s) gid %s fd %d\n",
			ctx->name, vvideo_ioctl_name(m->ioctl.cmd),
			c->id, vvideo_v4l2_cid(c->id),
			vvideo_dmabuf_gid_format(gids + gid, buf,
			sizeof(buf)), c->value);
		++gid;
	}
	if (ret)
		return ret;
	ext_size = req_ioctl->ext_size;
	if (p_count && (ext_size > VVIDEO2_REQ_IOCTL_EXT_SIZE ||
	    copy_to_user(m->ioctl.orig_user_ptr, req_ioctl->ext, ext_size))) {
		++vvideo_dev->errors.copy_to_user;
		ret = -EFAULT;
	}
	return ret;
}

static const struct vvideo_call_t vvideo_call_g_ext_ctrls = {
	.cmd = VIDIOC_G_EXT_CTRLS,
	.before = vvideo_msg_ext_ctrls_before,
	.after = vvideo_msg_ext_ctrls_after
};

static const struct vvideo_call_t vvideo_call_s_ext_ctrls = {
	.cmd = VIDIOC_S_EXT_CTRLS,
	.before = vvideo_msg_ext_ctrls_before,
	.after = vvideo_msg_ext_ctrls_after
};

static const struct vvideo_call_t vvideo_call_try_ext_ctrls = {
	.cmd = VIDIOC_TRY_EXT_CTRLS,
	.before = vvideo_msg_ext_ctrls_before,
	.after = vvideo_msg_ext_ctrls_after
};

/* Must be sized with the same constant as vvideo_dev_t::stats::ioctl::nr */
static const struct vvideo_call_t *const vvideo_calls[VVIDEO_MAX_IOCTL] = {
	// struct v4l2_capability
	[_IOC_NR(VIDIOC_QUERYCAP)] = &vvideo_call_querycap,
	// struct v4l2_fmtdesc does not need processing
	// [_IOC_NR(VIDIOC_ENUM_FMT)] = &vvideo_call_enum_fmt,
#ifdef VVIDEO_DEBUG
	// struct v4l2_format (spills into ext[])
	[_IOC_NR(VIDIOC_G_FMT)] = &vvideo_call_g_fmt,
#endif
#ifdef VVIDEO_S_FMT
	// struct v4l2_format (spills into ext[])
	[_IOC_NR(VIDIOC_S_FMT)] = &vvideo_call_s_fmt,
#endif
	// struct v4l2_requestbuffers
	[_IOC_NR(VIDIOC_REQBUFS)] = &vvideo_call_reqbufs,
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
#ifdef VVIDEO_DEBUG
	// struct v4l2_crop
	[_IOC_NR(VIDIOC_G_CROP)] = &vvideo_call_g_crop,
#endif
	// struct v4l2_crop
	[_IOC_NR(VIDIOC_S_CROP)] = &vvideo_call_s_crop,
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
 * This vvideo_msg_ioctl() routine copies data directly between
 * user space and PMEM memory shared between virtual machines. It is
 * inspired by video_usercopy() (see drivers/media/v4l2-core/v4l2-ioctl.c),
 * which uses a local-only buffer. It avoids a kernel memory allocation
 * and another copy.
 * The vvideo_dev_t::lock must be Locked, but will be released during
 * remote call
 */
static noinline int vvideo_msg_ioctl(struct vvideo_msg_t *m,
					const unsigned int cmd,
					const unsigned long arg)
{
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	struct vvideo_fe_t *fe = vl->fe;
	struct VVideo2Ioctl *req_ioctl = &m->req->u.ioctl;
	const struct vvideo_call_t *call = NULL;
	const unsigned int nr = _IOC_NR(cmd);
	int ret = 0;

	m->ioctl.cmd = cmd;
	m->ioctl.arg = arg;
	m->ioctl.async_mask = vvideo_dev->async ? fe->async_mask : 0;
	/* m->ioctl.orig_user_ptr is only set by some handlers */
	/* m->ioctl.vfid is only set by buffer handlers */

#ifdef VVIDEO_MSG_CHECKS
	if (!m->req) {
		char buf[VVIDEO_MSG_STRING];

		WARN(++vl->warnings.msg_req_null <= VVIDEO_WARN_MAX,
			"m->req NULL line %d for msg %p: %s\n",
			__LINE__, m, vvideo_msg_format(m, buf, sizeof(buf)));
		return -EFAULT;
	}
#endif
	req_ioctl->cmd = cmd;
	/* req_ioctl->arg_size set below */
	req_ioctl->ext_size = 0;
	/* req_ioctl->arg_value set below */

	VVIDEO_LOG("ctx %s %s, cmd %#x (%s), size %d, arg %#lx\n",
		   ctx->name, m->name, cmd, vvideo_ioctl_name(cmd),
		   _IOC_SIZE(cmd), arg);
	if (_IOC_DIR(cmd) == _IOC_NONE) {
		req_ioctl->arg_size = 0;
		req_ioctl->arg_value = arg;
	} else {
		req_ioctl->arg_size = _IOC_SIZE(cmd);
		req_ioctl->arg_value = 0;
		/*
		 * A non-v4l2_ext_controls struct is granted to fill both
		 * arg[] and ext[].
		 * The structures which require full buffer as the ones
		 * which are checked with VVIDEO_CHECK_SIZEOF2() above.
		 */
		if (_IOC_SIZE(cmd) > VVIDEO2_REQ_IOCTL_TOTAL_SIZE) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.arg_size,
			"ctx %s ioctl arg too large, size %u, cmd %#x (%s)\n",
				ctx->name, _IOC_SIZE(cmd), cmd,
				vvideo_ioctl_name(cmd));
			return -EINVAL;
		}
		if (_IOC_DIR(cmd) & _IOC_WRITE && copy_from_user(req_ioctl->arg,
		    (void __user *)arg, _IOC_SIZE(cmd))) {
			++vvideo_dev->errors.copy_from_user;
			return -EFAULT;
		}
	}
	/* Unlisted cmd values get no pre- or post-processing */
	if (nr < ARRAY_SIZE(vvideo_calls)) {
		++ctx->dev->stats.ioctl.nr[nr];
		if (vvideo_calls[nr] && vvideo_calls[nr]->cmd == cmd)
			call = vvideo_calls[nr];
	} else {
		++ctx->dev->stats.ioctl.other;
	}
	if (call && call->before) {
		ret = call->before(m);
		if (ret)
			return ret;
	}
	VVIDEO_LOG_VERBOSE(
	"ctx %s %s cmd %#x (%s), arg_value %#lx, arg_size %#x, ext_size %#x\n",
			   ctx->name, m->name, cmd, vvideo_ioctl_name(cmd),
			   req_ioctl->arg_value, _IOC_SIZE(cmd),
			   req_ioctl->ext_size);

	mutex_unlock(&vvideo_dev->lock);
#ifdef VVIDEO_ASYNC_CHECKSUMS
	if (m->async)
		m->checksum = vvideo_msg_checksum(m);
#endif
	m->req_num = VVIDEO2_REQ_IOCTL;
	ret = vvideo_msg_call(m, true /*interruptible*/, true /*abortable*/);
	mutex_lock(&vvideo_dev->lock);

	VVIDEO_LOG_VERBOSE("ctx %s %s cmd %#x (%s), ret %d\n",
			   ctx->name, m->name, cmd, vvideo_ioctl_name(cmd),
			   ret);
	if (call && call->after)
		ret = call->after(m, ret);
	if (ret)
		return ret;
	if (_IOC_DIR(cmd) & _IOC_READ && copy_to_user((void __user *)arg,
	    req_ioctl->arg, _IOC_SIZE(cmd))) {
		/* We do not attempt canceling */
		++vvideo_dev->errors.copy_to_user;
		return -EFAULT;
	}
	return ret;
}

static long vvideo_ctx_ioctl(struct vvideo_ctx_t *ctx, unsigned int cmd,
			     unsigned long arg)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	int ret;
	struct vvideo_msg_t *m;

	VVIDEO_LOG_VERBOSE("ctx %s, cmd %#x (%s), arg %#lx\n",
			   ctx->name, cmd, vvideo_ioctl_name(cmd), arg);
	m = vvideo_link_msg_get(vl, ctx, false /*!nonblocking*/);
	if (IS_ERR(m)) {
		if (cmd == VIDIOC_DQBUF) {
			++vvideo_dev->errors.msg_get;
			VVIDEO_LOG_VERBOSE(
			    "ctx %s, failed to allocate message for %s (%d)\n",
				ctx->name, vvideo_ioctl_name(cmd),
				(int)PTR_ERR(m));
		} else {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.msg_get,
			    "ctx %s, failed to allocate message for %s (%d)\n",
				ctx->name, vvideo_ioctl_name(cmd),
				(int)PTR_ERR(m));
		}
		return PTR_ERR(m);
	}
	/*
	 * vvideo_dev_t::lock is not used to serialize ioctl execution.
	 * It is only used to protect remote invocation preparation and
	 * the processing of results.
	 * vvideo_dev_t::lock will be released during the remote invocation,
	 * so that multiple threads can execute and sleep at the same time
	 * in backend.
	 */
	if (mutex_lock_interruptible(&vvideo_dev->lock)) {
		/* (kref A) put */
		vvideo_msg_release(m, VVIDEO_MSG_THREAD);
		++vvideo_dev->stats.ioctl_erestartsys;
		VVIDEO_LOG("ctx %s ERESTARTSYS\n", ctx->name);
		return -ERESTARTSYS;
	}
	ret = vvideo_msg_ioctl(m, cmd, arg);

	VVIDEO_LOG_VERBOSE("ctx %s %s, cmd %#x (%s), arg %#lx ret %d\n",
			   ctx->name, m->name, cmd, vvideo_ioctl_name(cmd), arg,
			   ret);
	/* (kref A) put */
	vvideo_msg_release(m, VVIDEO_MSG_THREAD);
	mutex_unlock(&vvideo_dev->lock);
	if (ret < 0) {
		++vvideo_dev->stats.ioctl_error;
		VVIDEO_LOG("ctx %s failed (%d)\n", ctx->name, ret);
	}
	return ret;
}

static unsigned int vvideo_msg_poll(struct vvideo_msg_t *m,
				     poll_table *poll_table,
				     unsigned int poll_cookie,
				     _Bool poll_has_list)
{
	struct vvideo_ctx_t *ctx = m->ctx;
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct VVideo2Poll *req_poll = &m->req->u.poll;
	int ret;

	req_poll->events = poll_requested_events(poll_table); /* _key */
	req_poll->cookie = poll_cookie;
	req_poll->does_not_wait = poll_does_not_wait(poll_table);
	req_poll->has_list = poll_has_list;
	mutex_unlock(&vvideo_dev->lock);
	m->req_num = VVIDEO2_REQ_POLL;
	ret = vvideo_msg_call(m, true /*interruptible*/, true /*abortable*/);
	mutex_lock(&vvideo_dev->lock);
	/*
	 * Return value must be a bitmap of POLLERR, POLLHUP,
	 * POLLIN, POLLRDNORM, POLLOUT, POLLWRNORM, ...
	 */
	return ret < 0 ? 0 : ret;
}

static unsigned int vvideo_ctx_poll(struct vvideo_ctx_t *ctx,
				     poll_table *poll_table,
				     unsigned int poll_cookie,
				     _Bool poll_has_list, _Bool async)
{
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_link_t *vl = vvideo_dev->link;
	struct vvideo_msg_t *m;
	int ret;

	m = vvideo_link_msg_get(vl, ctx, false /*!nonblocking*/);
	if (IS_ERR(m)) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.msg_get,
		    "ctx %s, failed to allocate message (%d)\n",
			ctx->name, (int)PTR_ERR(m));
		return POLLERR;	/* This is a poll mask */
	}
	if (mutex_lock_interruptible(&vvideo_dev->lock)) {
		/* (kref A) put */
		vvideo_msg_release(m, VVIDEO_MSG_THREAD);
		++vvideo_dev->stats.poll_erestartsys;
		VVIDEO_LOG("ctx %s ERESTARTSYS\n", ctx->name);
		return 0;	/* This is a poll mask */
	}
	/* For ioctls, m->async is set in some "before" handlers */
	m->async = async;
	ret = vvideo_msg_poll(m, poll_table, poll_cookie, poll_has_list);
	/* (kref A) put */
	vvideo_msg_release(m, VVIDEO_MSG_THREAD);
	mutex_unlock(&vvideo_dev->lock);
	return ret;	/* This is a poll mask */
}

/* vvideo_dev::ctx_lock must be Unlocked */

static struct vvideo_ctx_t *vvideo_dev_next_ctx(struct vvideo_ctx_iter_t *iter)
{
	struct vvideo_dev_t *vvideo_dev = iter->dev;
	unsigned long spinlock_flags;
	struct vvideo_ctx_t **ctx;

	if ((unsigned int) iter->ctx_num < VVIDEO_CTX_MAX) {
		ctx = iter->dev->ctx + iter->ctx_num;
		kref_put(&(*ctx)->kref, vvideo_ctx_kref_release);
	}
	spin_lock_irqsave(&vvideo_dev->ctx_lock, spinlock_flags);
	for (ctx = vvideo_dev->ctx + ++iter->ctx_num;
	     iter->ctx_num < VVIDEO_CTX_MAX; ++iter->ctx_num, ++ctx) {
		if (!*ctx)
			continue;
		kref_get(&(*ctx)->kref);
		break;
	}
	spin_unlock_irqrestore(&vvideo_dev->ctx_lock, spinlock_flags);
	if ((unsigned int) iter->ctx_num >= VVIDEO_CTX_MAX)
		return NULL;
	return *ctx;
}

/* vvideo_dev::ctx_lock must be Unlocked */

static struct vvideo_ctx_t *vvideo_dev_first_ctx(
		struct vvideo_dev_t *vvideo_dev, struct vvideo_ctx_iter_t *iter)
{
	iter->dev = vvideo_dev;
	iter->ctx_num = -1;
	return vvideo_dev_next_ctx(iter);
}

/* Only if vvideo_dev_next_ctx() did not return NULL yet */
/* vvideo_dev::ctx_lock must be Unlocked */

static void vvideo_dev_close_ctx(struct vvideo_ctx_iter_t *iter)
{
	struct vvideo_ctx_t *ctx;

	if ((unsigned int) iter->ctx_num >= VVIDEO_CTX_MAX)
		return;
	ctx = iter->dev->ctx[iter->ctx_num];
	kref_put(&ctx->kref, vvideo_ctx_kref_release);
}

/*
 * Release contexts which could not be released synchronously
 * because of lack of msg resources.
 */

static _Bool vvideo_link_pending_ctx_releases(vmq_link_t *link2, void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_fe_t *fe = vl->fe;
	struct vvideo_dev_t *vvideo_dev;
	int vvideo_dev_slot;

	VVIDEO_FOR_ALL_DEVS(vl, vvideo_dev_slot, vvideo_dev)
	{
		struct vvideo_ctx_t *ctx;
		struct vvideo_ctx_iter_t iter;

		mutex_lock(&fe->mutex);
		for (ctx = vvideo_dev_first_ctx(vvideo_dev, &iter); ctx;
		     ctx = vvideo_dev_next_ctx(&iter)) {
			int ret;

			if (!ctx->pending_release)
				continue;
			/*
			 * Make sure nobody else tries to free
			 * it in parallel.
			 * The slot cannot be re-used because
			 * pointer is still set.
			 */
			ctx->pending_release = false;
			mutex_unlock(&fe->mutex);
			VVIDEO_LOG("dev %s releasing pending ctx %s\n",
				   vvideo_dev->name, ctx->name);
			ret = vvideo_ctx_release(ctx, true /*nonblocking*/);
			mutex_lock(&fe->mutex);
			if (ret && vl->link_on) {
				/* Failed, but still link_on */
				ctx->pending_release = true;
				break;
			}
			++fe->stats.done_pending_releases;
			/* Release the vvideo_fops_open reference */
			kref_put(&ctx->kref, vvideo_ctx_kref_release);
			if (atomic_dec_and_test(&fe->pending_releases)) {
				vvideo_dev_close_ctx(&iter);
				mutex_unlock(&fe->mutex);
				return true; /* Stop link iteration */
			}
		}
		vvideo_dev_close_ctx(&iter);
		mutex_unlock(&fe->mutex);
	}
	return false; /* Continue link iteration */
}

static void vvideo_link_on(vmq_link_t *link2);

    /*
     * Perform link_on which
     * could not be performed immediately because
     * some struct vvideo_msg_t were still in use.
     */

static _Bool vvideo_link_pending_link_ons(vmq_link_t *link2, void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);

	if (!vl->pending_link_on)
		return false;	/* Continue link iteration */
	if (!vl->link_on && atomic_read(&vl->msgs_used))
		return false;	/* Continue link iteration */
	vl->pending_link_on = false;
	vvideo_link_on(link2);
	++vl->fe->stats.done_pending_link_ons;
	return false;	/* Continue link iteration */
}

/*----- v4l2_file_operations calls -----*/

/* Called from interrupt handler too */
/* vvideo_dev::ctx_lock must be Unlocked */

static void vvideo_ctx_kref_release(struct kref *kref)
{
	struct vvideo_ctx_t *ctx =
		container_of(kref, struct vvideo_ctx_t, kref);
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	unsigned long spinlock_flags;

	VVIDEO_ASSERT(vvideo_dev, !ctx->pending_release);
	spin_lock_irqsave(&vvideo_dev->ctx_lock, spinlock_flags);
	vvideo_dev->ctx[ctx->num] = NULL;
	spin_unlock_irqrestore(&vvideo_dev->ctx_lock, spinlock_flags);
	kfree(ctx);
}

/* vvideo_dev::ctx_lock must be Unlocked */

static int vvideo_fops_open(struct file *file)
{
	struct video_device *vfd = video_devdata(file);
	struct vvideo_dev_t *vvideo_dev = video_drvdata(file);
	int ret = 0;
	struct vvideo_ctx_t *ctx;
	struct vvideo_dmabuf_t *d;
	int d_slot;
	unsigned long spinlock_flags;

	VVIDEO_LOG("enter, device %s, %sblocking\n", vvideo_dev->name,
		   file->f_flags & O_NONBLOCK ? "non-" : "");
	++vvideo_dev->stats.open;
	/* Allocate memory for context */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.ctx_kmalloc,
			"device %s Not enough memory\n", vvideo_dev->name);
		return -ENOMEM;
	}
	ctx->dev = vvideo_dev;
	ctx->file = file;
	kref_init(&ctx->kref);
	spin_lock_init(&ctx->poll.spinlock);
	/* ctx->poll.list starts as NULL */
	/* ctx->poll.last_cookie starts at 0 */
	ctx->opener.pid = task_tgid_vnr(current);
	memcpy(ctx->opener.comm, current->comm, sizeof(ctx->opener.comm));

	spin_lock_irqsave(&vvideo_dev->ctx_lock, spinlock_flags);
	for (ctx->num = 0;
	     ctx->num < VVIDEO_CTX_MAX && vvideo_dev->ctx[ctx->num];
	     ++ctx->num) {
	}
	if (ctx->num >= VVIDEO_CTX_MAX) {
		spin_unlock_irqrestore(&vvideo_dev->ctx_lock, spinlock_flags);
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.ctx_max,
			"device %s out of contexts\n", vvideo_dev->name);
		kfree(ctx);
		return -EBUSY;
	}
	snprintf(ctx->name, sizeof(ctx->name), "%d.%d.%d",
		 vvideo_dev->link->number, vvideo_dev->minor, ctx->num);
	vvideo_dev->ctx[ctx->num] = ctx;
	spin_unlock_irqrestore(&vvideo_dev->ctx_lock, spinlock_flags);

	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		vvideo_dmabuf_reset(d);
	}
	ctx->dma_buf_last = VVIDEO_DMABUF_MAX - 1;
	ret = vvideo_ctx_open(ctx, file->f_flags);
	if (ret < 0) {
		VVIDEO_ERR_LIMITED(vvideo_dev->errors.ctx_open,
			"ctx %s vvideo_ctx_open() failed (%d)\n", ctx->name,
			   ret);
		/*
		 * "m" did not necessarily come back yet, as we
		 * could have received a Ctrl-C or other signal, so
		 * defer cleanup until it does. We must also release
		 * the ctx if it got opened after all, to avoid
		 * hanging contexts in backend, which would only
		 * be cured by link_off.
		 */
		kref_put(&ctx->kref, vvideo_ctx_kref_release);
		return ret;
	}
	v4l2_fh_init(&ctx->fh, vfd);
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);
	VVIDEO_LOG("ctx %s Success, ret %d\n", ctx->name, ret);
	return ret;
}

static struct list_head *vvideo_poll_wqh_head(const struct vvideo_poll_t *poll)
{
#if KERNEL_VERSION(4, 12, 0) > LINUX_VERSION_CODE
	return (struct list_head *) &poll->wqh.task_list;
#else
	return (struct list_head *) &poll->wqh.head;
#endif
}

/* ctx->poll.spinlock must be Unlocked */

static int vvideo_ctx_release_poll(struct vvideo_ctx_t *ctx)
{
	int busy = 0;
	int freed = 0;
	struct vvideo_poll_t *to_free = NULL;
	struct vvideo_poll_t **curr;
	unsigned long spinlock_flags;

	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	for (curr = &ctx->poll.list; *curr;) {
		struct vvideo_poll_t *poll = *curr;

		if (!list_empty(vvideo_poll_wqh_head(poll)) ||
		    atomic_read(&poll->users) > 0) {
			curr = &poll->next;
			++busy;
			continue;
		}
		/* Detach "poll" from ctx->poll.list */
		*curr = poll->next;
		/* Attach "poll" to "to_free" list */
		poll->next = to_free;
		to_free = poll;
		++freed;
	}
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
	while (to_free) {
		struct vvideo_poll_t *poll = to_free;

		to_free = to_free->next;
		/* Tell backend it can release resources for this poll */
		vvideo_ctx_poll(ctx, NULL /*poll_table*/, poll->cookie,
			false /*!poll_has_list*/, true /*async*/);
		/* Release ctx and free "poll" memory */
		/* Handler should never get called here */
		VVIDEO_ASSERT(ctx->dev, vvideo_kref_read(&ctx->kref) > 1);
		kref_put(&ctx->kref, vvideo_ctx_kref_release);
		kmem_cache_free(ctx->dev->link->fe->poll_kmem_cache, poll);
	}
	VVIDEO_LOG("busy %d freed %d\n", busy, freed);
	return busy;
}

/* ctx->poll.spinlock must be Unlocked */

static void vvideo_ctx_poll_state(struct vvideo_ctx_t *ctx,
			int *const busy, int *const idle, int *const queued,
			int *const total)
{
	unsigned long spinlock_flags;
	const struct vvideo_poll_t *poll;

	*busy = *idle = *queued = *total = 0;
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	for (poll = ctx->poll.list; poll; poll = poll->next) {
		++*total;
		if (!list_empty(vvideo_poll_wqh_head(poll))) {
			++*queued;
			if (atomic_read(&poll->users) > 0)
				++*busy;
		} else if (atomic_read(&poll->users) > 0)
			++*busy;
		else
			++*idle;
	}
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
}

static int vvideo_fops_release(struct file *file)
{
	struct vvideo_ctx_t *ctx = vvideo_fh_to_vvideo_ctx(file->private_data);
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_fe_t *fe = vvideo_dev->link->fe;
	int ret;

	VVIDEO_LOG("ctx %s enter\n", ctx->name);
	VVIDEO_ASSERT(vvideo_dev, ctx == vvideo_dev->ctx[ctx->num]);
	++vvideo_dev->stats.release;
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);

	/* Returns the number of busy entries */
	ret = vvideo_ctx_release_poll(ctx);
	VVIDEO_ASSERT(vvideo_dev, !ret);

#ifdef VVIDEO_DPBS
	vvideo_ctx_dpbs_release(ctx);
#endif
	/* Maybe we could do this only after remote call returns */
	mutex_lock(&vvideo_dev->lock);
	vvideo_dmabuf_release_all(ctx);
	mutex_unlock(&vvideo_dev->lock);

	ret = vvideo_ctx_release(ctx, false /*!nonblocking*/);
	mutex_lock(&fe->mutex);
	if (ret) {
		ctx->pending_release = true;
		atomic_inc(&fe->pending_releases);
		++fe->stats.pending_releases;
		/*
		 * Do not schedule_work until some msg freed
		 * or link lost, otherwise the work would
		 * busy loop in vvideo_ctx_release() retries.
		 */
	}
	/*
	 * Do this trace before freeing "ctx" and before releasing
	 * mutex, which holds destruction of ctx in case "work" runs,
	 * notices ctx->pending_release and does it.
	 */
	VVIDEO_LOG("ctx %s finished\n", ctx->name);
	mutex_unlock(&fe->mutex);
	if (!ret)
		/* Release the vvideo_fops_open reference */
		kref_put(&ctx->kref, vvideo_ctx_kref_release);
	return 0;
}

static void vvideo_poll_init(struct vvideo_poll_t *poll,
		struct vvideo_ctx_t *ctx, poll_table *poll_table)
{
	unsigned long spinlock_flags;

	kref_get(&ctx->kref);
	poll->ctx = ctx;
	init_waitqueue_head(&poll->wqh);
	poll->requested_events = poll_requested_events(poll_table); /* _key */
	poll->cookie = ctx->num | (atomic_inc_return(&ctx->poll.last_cookie) <<
		VVIDEO_CTX_MAX_LOG);
	atomic_set(&poll->users, 1);
	/* Attach "poll" to ctx->poll.list */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	poll->next = ctx->poll.list;
	ctx->poll.list = poll;
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
}

static unsigned int vvideo_fops_poll(struct file *file,
				     poll_table *poll_table)
{
	struct vvideo_ctx_t *ctx = vvideo_fh_to_vvideo_ctx(file->private_data);
	struct vvideo_dev_t *vvideo_dev = ctx->dev;
	struct vvideo_poll_t *poll;
	unsigned int poll_cookie;
	_Bool poll_has_list;

	VVIDEO_LOG("ctx %s poll_table %p _qproc %ps events %#lx\n",
		ctx->name, poll_table, poll_table ? poll_table->_qproc : NULL,
		poll_requested_events(poll_table)); /* _key */
	vvideo_ctx_release_poll(ctx);
	if (!poll_does_not_wait(poll_table)) {
		/* New request, allocate new descriptor */
		poll = kmem_cache_alloc(ctx->dev->link->fe->poll_kmem_cache,
			GFP_ATOMIC);
		if (!poll) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.kmem_cache_alloc,
				"ctx %s kmem_cache_alloc\n", ctx->name);
			return 0;
		}
		++vvideo_dev->stats.poll_1st;
		vvideo_poll_init(poll, ctx, poll_table);
	} else {
		unsigned long spinlock_flags;

		/* Old request, find existing descriptor */
		spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
		for (poll = ctx->poll.list; poll; poll = poll->next) {
			if (poll->requested_events !=
			    poll_requested_events(poll_table)) /* _key */
				continue;
			atomic_inc(&poll->users);
			break;
		}
		spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
		if (!poll) {
			VVIDEO_ERR_LIMITED(vvideo_dev->errors.poll_find,
				"ctx %s poll_find %pS\n", ctx->name,
				poll_table);
			return 0;
		}
		++vvideo_dev->stats.poll_2nd;
	}
	VVIDEO_LOG("poll %s.%x head %p next %p prev %p\n",
		ctx->name, poll->cookie,
		vvideo_poll_wqh_head(poll), vvideo_poll_wqh_head(poll)->next,
		vvideo_poll_wqh_head(poll)->prev);
	poll_wait(file, &poll->wqh, poll_table);	/* void */
	poll_has_list = !list_empty(vvideo_poll_wqh_head(poll));
	VVIDEO_LOG("poll %s.%x has_list %d next %p prev %p\n", ctx->name,
		poll->cookie, poll_has_list, vvideo_poll_wqh_head(poll)->next,
		vvideo_poll_wqh_head(poll)->prev);
	poll_cookie = poll->cookie;
	/* We do not need to keep a ref to "poll" anymore */
	atomic_dec(&poll->users);
	return vvideo_ctx_poll(ctx, poll_table, poll_cookie, poll_has_list,
		false /*!async*/);
}

static long vvideo_fops_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	struct vvideo_ctx_t *ctx = vvideo_fh_to_vvideo_ctx(file->private_data);

	VVIDEO_LOG("ctx %s cmd %#x (%s) %sblocking\n",
		   ctx->name, cmd, vvideo_ioctl_name(cmd),
		   file->f_flags & O_NONBLOCK ? "non-" : "");
	return vvideo_ctx_ioctl(ctx, cmd, arg);
}

static const struct v4l2_file_operations vvideo_fops = {
	.owner = THIS_MODULE,
	.open = vvideo_fops_open,
	.release = vvideo_fops_release,
	.poll = vvideo_fops_poll,
	.unlocked_ioctl = vvideo_fops_ioctl,
};

/*----- VMQ event handlers (callbacks) -----*/

static void vvideo_link_on(vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);

	if (!vl->link_on)
		++vl->session;
	vl->link_on = true;
	mutex_lock(&vl->abort_lock);
	/* Check for possible duplicate calls */
	if (!vl->abort_msg) {
		unsigned long spinlock_flags;

		vl->abort_msg = vvideo_link_msg_get(
			vl, NULL, false /*!nonblocking*/);
		WARN_ON(IS_ERR(vl->abort_msg));
		if (IS_ERR(vl->abort_msg)) {
			vl->abort_msg = NULL;
			mutex_unlock(&vl->abort_lock);
			return;
		}
		/* (kref A) put. Current thread does not use this object */
		/* (kref C) get. Overall m->kref does not change */
		spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
		vl->abort_msg->abort = true;
		vl->abort_msg->pid = 0;
		spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);

		/* Get clean /proc/nk/vvideo2-fe if PMEM was not zeroes */
		VVIDEO_ZERO(vl->abort_msg->req);
	}
	mutex_unlock(&vl->abort_lock);
	wake_up_all(&vl->open_wait);
}

static void vvideo_cb_link_on(vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_fe_t *fe = vl->fe;

	++vl->stats.link_on;
	VVIDEO_LOG("link %s entered %d\n", vl->name, vl->stats.link_on);
	if (!vl->link_on && atomic_read(&vl->msgs_used)) {
		/*
		 * There could be some old requests which lost race to
		 * list membership (vipc_list_add()) before abort came,
		 * so their waiter->occurred was never set, and hence
		 * they are awaiting a return_msg which will never come.
		 * Try to awake them here. When they all release msgs_used,
		 * the link will eventually enter link_on state.
		 */
		vipc_ctx_abort_calls(&VVIDEO_LINK(link2)->vipc_ctx);
		vl->pending_link_on = true;
		atomic_inc(&fe->pending_link_ons);
		++fe->stats.pending_link_ons;
		return;
	}
	vvideo_link_on(link2);
	VVIDEO_LOG("link %s exited\n", vl->name);
}

static void vvideo_cb_link_off(vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_fe_t *fe = vl->fe;

	++vl->stats.link_off;
	VVIDEO_LOG("link %s entered %d\n", vl->name, vl->stats.link_off);
	vl->link_on = false;
	vl->pending_link_on = false;
	/*
	 * This call also aborts ABORTs, which makes them not found
	 * as reply. However vl->abort_msg will not be freed by
	 * vvideo_cb_return_notify() because we hold a kref on
	 * it, unlike regular threads which will release their
	 * kref when returning to user mode.
	 */
	vipc_ctx_abort_calls(&VVIDEO_LINK(link2)->vipc_ctx);
	if (atomic_read(&fe->pending_releases) > 0)
		schedule_work(&fe->work);
	VVIDEO_LOG("link %s releasing abort_msg\n", vl->name);
	mutex_lock(&vl->abort_lock);
	/*
	 * vvideo_cb_link_off() also gets called at init time before we get
	 * a link on and a vl->abort_msg, so check for this situation.
	 */
	if (vl->abort_msg) {
		/* (kref C) put */
		vvideo_msg_release(vl->abort_msg, VVIDEO_MSG_ABORT);
		vl->abort_msg = NULL;
	}
	mutex_unlock(&vl->abort_lock);
	/*
	 * On link_off, the pending requests for which no thread is waiting
	 * anymore are not going to be freed automatically, because even if
	 * vvideo_cb_return_notify() was called, vmq_return_msg_receive()
	 * would not return them one by one. So we must do cleanup manually.
	 * We should not try to return msgs back to VMQ.
	 * To avoid races with still operating vvideo_ctx_ioctl(), defer
	 * this to vvideo_cb_link_off_completed().
	 */
	VVIDEO_LOG("link %s exited\n", vl->name);
}

static void vvideo_cb_link_off_completed(vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);

	++vl->stats.link_off_completed;
	VVIDEO_LOG("link %s count %d\n", vl->name,
		vl->stats.link_off_completed);
	/* See comment in vvideo_cb_link_off() */
	vvideo_link_msg_cleanup(vl);
	VVIDEO_LOG("link %s exited\n", vl->name);
}

/* Interrupt-level callback */

static void vvideo_cb_sysconf_notify(vmq_links_t *links)
{
	struct vvideo_fe_t *fe = VVIDEO_LINKS_FE(links);

	VVIDEO_LOG("entered\n");
	fe->sysconf_notify = true;
	schedule_work(&fe->work);
}

static const char *vvideo_req_format(const struct VVideo2Request *req,
				     char *buf, size_t bufsize)
{
	const char *s = vvideo_req_full(req);	/* Never NULL */
	int len;

	len = snprintf(buf, bufsize, "%s ", s);
	if ((unsigned int) len >= bufsize)
		return buf;
	if (req->req_num == VVIDEO2_REQ_IOCTL)
		vvideo_ioctl_format(req->u.ioctl.cmd, req->u.ioctl.arg,
				buf + len, bufsize - len);
	return buf;
}

static _Bool vvideo_ctx_stale_pool_cookie(struct vvideo_ctx_t *ctx,
					unsigned int poll_cookie)
{
	/*
	 * Once poll.last_cookie becomes larger than reserved space
	 * in poll_cookie, all not found cookies will be "stale", and
	 * when it overflows, all not found cookies will be "invalid"
	 * for a short moment.
	 */
	return (poll_cookie >> VVIDEO_CTX_MAX_LOG) <=
		atomic_read(&ctx->poll.last_cookie);
}

/* Interrupt-level callback */
/* vvideo_dev::ctx_lock must be Unlocked */

static void vvideo_link_receive_msg(vmq_link_t *link2,
				    struct VVideo2Request *req)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	const unsigned int req_num = req->req_num;
	const unsigned int be_context = req->be_context;
	const unsigned int minor = be_context & vl->dev_mask;
	const unsigned long poll_revents = req->u.poll.events;
	const unsigned int poll_cookie = req->u.poll.cookie;
	const unsigned int ctx_num = poll_cookie & (VVIDEO_CTX_MAX - 1);
	struct vvideo_dev_t *vvideo_dev;
	struct vvideo_ctx_t *ctx;
	struct vvideo_poll_t *poll;
	unsigned long spinlock_flags;

	vmq_msg_free(link2, req);
	if (req_num != VVIDEO2_REQ_EVENT) {
		VVIDEO_WARN_LIMITED(vl->warnings.wrong_event,
			"link %s Invalid request %d (%s)\n",
			vl->name, req_num, vvideo_req_name(req_num));
		return;
	}
	if (minor >= vl->configs.count) {
		VVIDEO_WARN_LIMITED(vl->warnings.wrong_minor,
			"link %s Minor %d too high (>= %d)\n",
			vl->name, minor, vl->configs.count);
		return;
	}
	vvideo_dev = vl->devs + minor;
	/* Make sure ctx does not get freed during use */
	spin_lock_irqsave(&vvideo_dev->ctx_lock, spinlock_flags);
	ctx = vvideo_dev->ctx[ctx_num];
	if (ctx && ctx->be_context == be_context)
		kref_get(&ctx->kref);
	else
		ctx = NULL;
	spin_unlock_irqrestore(&vvideo_dev->ctx_lock, spinlock_flags);
	if (!ctx) {
		VVIDEO_WARN_LIMITED(vvideo_dev->warnings.poll_ctx,
			"dev %s be_context %#x not found\n",
			vvideo_dev->name, be_context);
		return;
	}
	/* Find "poll" */
	spin_lock_irqsave(&ctx->poll.spinlock, spinlock_flags);
	for (poll = ctx->poll.list; poll; poll = poll->next) {
		if (poll->cookie != poll_cookie)
			continue;
		atomic_inc(&poll->users);
		break;
	}
	spin_unlock_irqrestore(&ctx->poll.spinlock, spinlock_flags);
	if (!poll) {
		if (vvideo_ctx_stale_pool_cookie(ctx, poll_cookie))
			++vvideo_dev->stats.poll_stale;
		else
			VVIDEO_WARN_LIMITED(vvideo_dev->warnings.poll_cookie,
				"ctx %s Did not find poll cookie %#x\n",
				ctx->name, poll_cookie);
		goto exit;
	}
	/*
	 * Signal the wait queue on which some client
	 * might sleep if we called poll_wait() for him.
	 */
	++vvideo_dev->stats.poll_event;
	VVIDEO_LOG("poll %s.%x events %#lx revents %#lx empty %d\n",
		ctx->name, poll_cookie, poll->requested_events, poll_revents,
		list_empty(vvideo_poll_wqh_head(poll)));
	wake_up_poll(&poll->wqh, poll_revents);
	atomic_dec(&poll->users);
	/*
	 * Cannot free "poll" yet, because do_select()
	 * will still call poll_freewait(), which will call
	 * remove_wait_queue() and access the poll->wqh spinlock.
	 */
exit:
	kref_put(&ctx->kref, vvideo_ctx_kref_release);
}

/* Interrupt-level callback */

static void vvideo_cb_receive_notify(vmq_link_t *link2)
{
	void *msg;

	VVIDEO_LOG("link %s got msg\n", VVIDEO_LINK(link2)->name);
	while (!vmq_msg_receive(link2, &msg))
		vvideo_link_receive_msg(link2, msg);
}

/* Interrupt-level callback */

static void vvideo_req_return(struct VVideo2Request *req, vmq_link_t *link2)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_msg_t *m = vl->msgs + vmq_msg_slot(vl->link, req);
	struct vvideo_dev_t *vvideo_dev = m->ctx ? m->ctx->dev : NULL;
	char buf[VVIDEO_REQ_STRING];

#ifdef VVIDEO_RECORD
#ifdef VVIDEO_RECORD_REPLIES
	if (vl->fe->recording && vvideo_dev)
		vvideo_dev_record(vvideo_dev, -1, req);
#endif
#endif
	/*
	 * Enter state Replied (IVa) or Reply-Ignored (IVb), depending
	 * on whether the thread still waits for reply (kref=2) or not
	 * (kref=1).
	 */
	m->replied = true;
	if (!vipc_ctx_process_reply_ex(&vl->vipc_ctx, m))
		goto exit_release;
	/*
	 * State is Reply-Ignored (IVb).
	 * kref=1, message will be freed by release.
	 */
	VVIDEO_LOG("link %s %s reply %s pid %d dev %s to req %s\n",
		vl->name, m->async ? "async" : "ignored", m->name,
		m->pid, vvideo_dev ? vvideo_dev->name : "None",
		vvideo_req_format(req, buf, sizeof(buf)));
	if (!vvideo_dev) {
		VVIDEO_WARN_LIMITED(vl->warnings.msg_ignored_null_dev,
			"NULL device for msg %p: %s\n",
			m, vvideo_msg_format(m, buf, sizeof(buf)));
	} else if (m->async) {
#ifdef VVIDEO_ASYNC_CHECKSUMS
		struct v4l2_requestbuffers *rb = (void *) req->u.ioctl.arg;
		unsigned long checksum;
#endif

		++m->ctx->dev->stats.async_reply;
		if (m->req_num != VVIDEO2_REQ_IOCTL)
			goto exit;
#ifdef VVIDEO_ASYNC_CHECKSUMS
		if (req->u.ioctl.cmd == VIDIOC_REQBUFS &&
#if KERNEL_VERSION(4, 20, 0) <= LINUX_VERSION_CODE
			rb->count == 0 && rb->capabilities) { /* } */
			rb->capabilities = 0;
#else
			rb->count == 0 && rb->reserved[0]) {
			rb->reserved[0] = 0;
#endif
			++m->ctx->dev->stats.reqbufs_0;
			checksum = vvideo_msg_checksum(m);
		} else
			checksum = vvideo_msg_checksum(m);
		if (checksum != m->checksum) {
			VVIDEO_WARN_LIMITED(vvideo_dev->warnings.async_checksum,
"Async req buffer modified: link %s async reply %s pid %d dev %s to req %s\n",
				vl->name, m->name, m->pid, vvideo_dev->name,
				vvideo_req_format(req, buf, sizeof(buf)));
		}
#endif
		if (m->req->result && m->req->result != -ESTALE) {
			VVIDEO_WARN_LIMITED(vvideo_dev->warnings.async_failure,
	"Async req failed %d: link %s async reply %s pid %d dev %s to req %s\n",
				m->req->result, vl->name,
				m->name, m->pid, vvideo_dev->name,
				vvideo_req_format(req, buf, sizeof(buf)));
		}
	} else if (m->req_num == VVIDEO2_REQ_OPEN
		    && m->req->be_context != vvideo_dev->minor) {
		++vvideo_dev->stats.lost_open;
		m->ctx->be_context = m->req->be_context;
		kref_get(&m->ctx->kref);
		m->ctx->pending_release = true;
		atomic_inc(&vl->fe->pending_releases);
		/* Decremented at release */
		atomic_inc(&vvideo_dev->open_count);
		++vl->fe->stats.pending_releases;
		schedule_work(&vl->fe->work);
	} else {
		++vvideo_dev->stats.ignored_reply;
		VVIDEO_LOG("Ignored reply to msg: %s\n",
			vvideo_msg_format(m, buf, sizeof(buf)));
	}
exit:
	m->ignored = true;
exit_release:
	/* (kref B) put */
	vvideo_msg_release(m, VVIDEO_MSG_VMQ);
}

/* Interrupt-level callback */

static void vvideo_cb_return_notify(vmq_link_t *link2)
{
	void *req;	/* struct VVideo2Request* */

	while (!vmq_return_msg_receive(link2, &req))
		vvideo_req_return(req, link2);
}

static void vvideo_work(struct work_struct *work)
{
	struct vvideo_fe_t *fe =
		container_of((struct work_struct *)work,
			     struct vvideo_fe_t, work);
	VVIDEO_LOG("entered sysconf %d pending_releases %d\n",
		   fe->sysconf_notify, atomic_read(&fe->pending_releases));
	if (fe->sysconf_notify) {
		fe->sysconf_notify = false;
		vmq_links_sysconf(fe->links);
	}
	if (atomic_read(&fe->pending_releases)) {
		vmq_links_iterate(fe->links, vvideo_link_pending_ctx_releases,
				  NULL);
		/*
		 * Release work is not always completed, for example if we run
		 * out of msgs. Do not re-schedule immediately or we might enter
		 * an infinite loop; wait for msg release first.
		 */
		if (atomic_read(&fe->pending_releases))
			VVIDEO_LOG("still %d pending_releases\n",
				   atomic_read(&fe->pending_releases));
	}
	if (atomic_read(&fe->pending_link_ons)) {
		atomic_set(&fe->pending_link_ons, 0);
		vmq_links_iterate(fe->links,
			vvideo_link_pending_link_ons, NULL);
		if (atomic_read(&fe->pending_link_ons))
			VVIDEO_LOG("still %d pending_link_ons\n",
				atomic_read(&fe->pending_link_ons));
	}
}

static const vmq_callbacks_t vvideo_callbacks = {
	.link_on = vvideo_cb_link_on,
	.link_off = vvideo_cb_link_off,
	.link_off_completed = vvideo_cb_link_off_completed,
	.sysconf_notify = vvideo_cb_sysconf_notify,
	.receive_notify = vvideo_cb_receive_notify,
	.return_notify = vvideo_cb_return_notify,
	.get_tx_config = vvideo_cb_get_xx_config,
	.get_rx_config = NULL
};

static const vmq_xx_config_t vvideo_rx_config = {
	.msg_count = 4,
	.msg_max = sizeof(struct VVideo2Request),
	.pdev_level = VMQ_PDEV_NONE
};

/*----- /proc/nk/vvideo2-fe -----*/

static inline const struct vvideo_config_t *
vvideo_dev_config(const struct vvideo_dev_t *vvideo_dev)
{
	return vvideo_dev->link->configs.configs + vvideo_dev->minor;
}

#ifdef CONFIG_PROC_FS

struct vvideo_proc_t {
	struct seq_file *seq;
	_Bool ext;
};

static void vvideo_ctx_proc_show(struct vvideo_ctx_t *ctx,
				 struct vvideo_proc_t *p)
{
	struct seq_file *seq = p->seq;
	int dmabufs_set = 0;
	int dmabufs_valid = 0;
	const struct vvideo_dmabuf_t *d;
	int d_slot;
	int busy, idle, queued, total;

	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		if (vvideo_dmabuf_is_set(d))
			++dmabufs_set;
		if (d->valid)
			++dmabufs_valid;
	}
	vvideo_ctx_poll_state(ctx, &busy, &idle, &queued, &total);
	seq_printf(seq,
		" Ctx %s pend_rel %d dmabufs %d/%d poll %d (%d %d %d) %u/%s\n",
		ctx->name, ctx->pending_release, dmabufs_valid, dmabufs_set,
		total, busy, idle, queued, ctx->opener.pid, ctx->opener.comm);
	if (!p->ext)
		return;
	VVIDEO_FOR_ALL_DMABUFS(ctx, d_slot, d)
	{
		char buf[VVIDEO_GID_STRING];

		if (!vvideo_dmabuf_is_set(d))
			continue;
		seq_printf(seq,
			" %2d %d %16p %2d %2d %3d %3d %2d/%d %2d %s %s\n",
			d_slot, d->valid, d->dma_buf, d->fd,
			(int)vvideo_kref_read(&d->kref), d->saved_f_count,
			(int)file_count(d->dma_buf->file), d->buf_index,
			d->plane, d->buf_type, d->buf_type ?
			vvideo_v4l2_buf_type_short(d->buf_type) : "NONE",
			vvideo_dmabuf_gid_format(&d->gid, buf, sizeof(buf)));
	}
}

static void vvideo_dev_proc_show(struct vvideo_dev_t *vvideo_dev,
				 struct vvideo_proc_t *p)
{
	struct seq_file *seq = p->seq;
	struct vvideo_link_t *vl = vvideo_dev->link;
	const struct vdev_errors *e = &vvideo_dev->errors;
	const struct vdev_warnings *w = &vvideo_dev->warnings;
	const struct vdev_stats *s = &vvideo_dev->stats;
	const _Bool errors = vvideo_non_zero(e, sizeof(*e));
	const _Bool warnings = vvideo_non_zero(w, sizeof(*w));
	const _Bool stats = vvideo_non_zero(s, sizeof(*s));
	const struct video_device *vfd = vvideo_dev->vfd;
	const struct vvideo_config_t *config = vvideo_dev_config(vvideo_dev);
	char path[32];
	struct vvideo_ctx_t *ctx;
	struct vvideo_ctx_iter_t iter;
	int nr;

	snprintf(path, sizeof(path), "/dev/video%d", vfd->num);
	seq_printf(seq, "%-4s %2d %2d %2d %4d %-13s %2d %2d %c %3x %3x %s\n",
		   vvideo_dev->name, vmq_peer_osid(vl->link),
		   atomic_read(&vvideo_dev->open_count), vfd->index,
		   vfd->minor, path, config->orig_node, config->final_node,
		   "AS"[!vvideo_dev->async], vvideo_dev->reqbufs_0_ok,
		   vvideo_dev->streamon_ok, vfd->name);

#ifdef VVIDEO_RECORD
	if (p->ext)
		vvideo_dev_record_seq(vvideo_dev, seq);
#endif
	for (ctx = vvideo_dev_first_ctx(vvideo_dev, &iter); ctx;
	     ctx = vvideo_dev_next_ctx(&iter))
		vvideo_ctx_proc_show(ctx, p);
	if (errors)
		seq_puts(seq, " Errors:");
#define X(x)                                                                   \
do {                                                                   \
	if (e->x)                                                      \
		seq_printf(seq, " " #x ":%d", e->x);                   \
} while (0)

	X(arg_size);
	X(copy_from_user);
	X(ext_size);
	X(fd2gid);
	X(ext_gid);
	X(fd_negative);
	X(dmabuf_slots);
	X(dma_buf_get);
	X(dmabuf_export);
	X(dmabuf_import);
	X(dma_buf_fd);
	X(not_dmabuf);
	X(copy_to_user);
	X(xirq_attach);
	X(remap);
	X(gid_invalid);
	X(find_dma_buf);
	X(epipe);
	X(msg_get);
	X(lost_call);
	X(ctx_kmalloc);
	X(ctx_max);
	X(ctx_open);
	X(req_open);
	X(edid_blocks);
	X(kmem_cache_alloc);
	X(poll_find);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	X(export_fence);
#endif
#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
	X(get_be_version);
	X(is_compatible);
	X(open_release);
#endif
#ifdef VVIDEO_DPBS
	X(dpbs.get);
	X(dpbs.import);
	X(dpbs.multiple);
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
		seq_printf(seq, " " #x ":%d", w->x);                   \
} while (0)

	X(unknown_cid);
	X(failed_assert);
	X(unexport_gid);
	X(abort_failed);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	X(import_fence);
	X(unexport_fence);
#endif
#ifdef VVIDEO_ASYNC_CHECKSUMS
	X(async_checksum);
#endif
	X(async_failure);
#ifdef VVIDEO_MSG_CHECKS
	X(kref_not_2);
	X(no_reply_kref_0);
#endif
	X(poll_ctx);
	X(poll_cookie);
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
		seq_printf(seq, " " #x ":%d", s->x);                   \
} while (0)

#define XI(x)                                                                  \
do {                                                                   \
	if (s->x)                                                      \
		seq_printf(seq, " %s:%d", &#x[6], s->x);               \
} while (0)

	X(open);
	X(release);
	X(poll_1st);
	X(poll_2nd);
	X(abort);
	X(fd2gid);
	X(fd_multiple);
	X(gid2fd);
	X(init2);
	X(init3);
	X(init4);
	X(initN);
	X(export0);
	X(export1);
	X(exportN);
	X(unexport0);
	X(unexport1);
	X(unexportN);
	X(open_erestartsys);
	X(open_eintr);
	X(call_erestartsys);
	X(call_eintr);
	X(ioctl_erestartsys);
	X(ioctl_error);
	X(poll_erestartsys);
	X(no_abort_msg);
	X(async_msg);
	X(sync_msg);
	X(async_reply);
#ifdef VVIDEO_ASYNC_CHECKSUMS
	X(reqbufs_0);
#endif
	X(ignored_reply);
	X(lost_open);
	X(streamoff_pending);
	X(poll_event);
	X(fences_ok);
	X(fences_removed);
	X(poll_stale);

	for (nr = 0; nr < VVIDEO_MAX_IOCTL; ++nr) {
		if (!s->ioctl.nr[nr])
			continue;
		seq_printf(seq, " %s:%d", vvideo_ioctl_name_ex(nr, 1) + 7,
			   s->ioctl.nr[nr]);
	}
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	XI(ioctl.in_fence);
	XI(ioctl.out_fence);
#endif
	XI(ioctl.single_planar);
	XI(ioctl.other);
#ifdef VVIDEO_DPBS
	X(dpbs.s_ctrl);
	X(dpbs.import);
#endif
	X(deleted_gid);
#undef XI
#undef X
	if (stats)
		seq_puts(seq, "\n");
}

static _Bool vvideo_link_proc_show(vmq_link_t *link2, void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_proc_t *p = cookie;
	struct seq_file *seq = p->seq;
	const _Bool vl_warnings = vvideo_non_zero(&vl->warnings,
							sizeof(vl->warnings));
	const _Bool vl_stats = vvideo_non_zero(&vl->stats,
							sizeof(vl->stats));
	struct vvideo_dev_t *vvideo_dev;
	int vvideo_dev_slot;
	struct vvideo_msg_t *m;
	unsigned long spinlock_flags;
	int m_slot;

	seq_printf(seq,
" configs %d msgs/used %d/%d link_on %d session %u pending_link_on %d\n",
		vl->configs.count,
		vl->xx_config.msg_count,
		atomic_read(&vl->msgs_used),
		vl->link_on,
		vl->session,
		vl->pending_link_on);

	if (vl_warnings)
		seq_puts(seq, " Warnings:");

#define X(x)	do {if (vl->warnings.x) seq_printf(seq, \
			" " #x ":%d", vl->warnings.x); } while (0)
	X(failed_assert);
#ifdef VVIDEO_MSG_CHECKS
	X(msg_req_null);
	X(msg_still_used);
#endif
	X(msg_ignored_null_dev);
	X(wrong_event);
	X(wrong_minor);
#undef X
	if (vl_warnings)
		seq_puts(seq, "\n");

	if (vl_stats)
		seq_puts(seq, " Stats:");

#define X(x)	do {if (vl->stats.x) seq_printf(seq, \
			" " #x ":%d", vl->stats.x); } while (0)
	X(link_on);
	X(link_off);
	X(link_off_completed);
	X(link_timeout);
#undef X

	if (vl_stats)
		seq_puts(seq, "\n");

	VVIDEO_FOR_ALL_DEVS(vl, vvideo_dev_slot, vvideo_dev)
	{
		vvideo_dev_proc_show(vvideo_dev, p);
	}
	seq_printf(seq, " Sl %s\n", vvideo_msg_legend);
	spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
	VVIDEO_FOR_ALL_MSGS(vl, m_slot, m)
	{
		char buf[VVIDEO_MSG_STRING];

		if (!vvideo_kref_read(&m->kref))
			continue;
		/* (kref D) get */
		kref_get(&m->kref);
		spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);
		seq_printf(seq, " %2d %s\n",
			   m_slot, vvideo_msg_format(m, buf, sizeof(buf)));
		/* (kref D) put */
		kref_put(&m->kref, vvideo_msg_kref_release);
		spin_lock_irqsave(&vl->msg_lock, spinlock_flags);
	}
	spin_unlock_irqrestore(&vl->msg_lock, spinlock_flags);
	return false;
}

static int vvideo_seq_proc_show(struct seq_file *seq, void *v)
{
	/* For seq_file, file->private_data points to struct seq_file */
	struct vvideo_fe_t **fep = (struct vvideo_fe_t **)seq->private;
	struct vvideo_fe_t *fe = *fep;
	_Bool warnings = vvideo_non_zero(&fe->warnings, sizeof(fe->warnings));
	const _Bool stats = vvideo_non_zero(&fe->stats, sizeof(fe->stats));
	struct vvideo_proc_t proc;

	(void)v;
	proc.seq = seq;
	proc.ext = (void *)fep != (void *)fe;
	seq_puts(seq, "vVideo v2 frontend (revision 2021-06-04)\n");
	seq_puts(seq,
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
		" VF"
#else
		" !VF"
#endif
#ifdef VVIDEO_MSG_CHECKS
		" MC"
#else
		" !MC"
#endif
#ifdef VVIDEO_MSG_TRACKING
		" MT"
#else
		" !MT"
#endif
#ifdef VVIDEO_S_FMT
		" SF"
#else
		" !SF"
#endif
#ifdef VVIDEO_RECORD
		" RE"
#else
		" !RE"
#endif
#ifdef VVIDEO_RECORD_REPLIES
		" RR"
#else
		" !RR"
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
#ifdef VVIDEO_ASYNC_CHECKSUMS
		" AC"
#else
		" !AC"
#endif
#ifdef VVIDEO_DEVICE_CAPS
		" DC"
#else
		" !DC"
#endif
#ifdef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
		" DV"
#else
		" !DV"
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
		VVIDEO_CTX_MAX_LOG,
		VVIDEO_ERR_MAX,
		VVIDEO_WARN_MAX,
#ifdef VVIDEO_RECORD
		VVIDEO_RECORD_MAX,
		VVIDEO_RECORD_DEFAULT,
#else
		0, 0,
#endif
		VVIDEO_REQ_STRING,
#ifdef VVIDEO_LOCAL_GID
		0
#else
		VVIDEO_VDMAHEAP_GRACE_DELAY_MS
#endif
	);
	seq_printf(seq,
		   "links %d dev_num %d async_mask %#x"
#ifdef VVIDEO_RECORD
		   " recording %d"
#endif
		   " pending_releases %d pending_link_ons %d\n",
		   fe->link_count, fe->dev_num, fe->async_mask,
#ifdef VVIDEO_RECORD
		   fe->recording,
#endif
		   atomic_read(&fe->pending_releases),
		   atomic_read(&fe->pending_link_ons));

	if (warnings)
		seq_puts(seq, "Warnings:");

#define X(x)                                                                   \
	do {                                                                   \
		if (fe->warnings.x)                                            \
			seq_printf(seq, " " #x ":%d\n", fe->warnings.x);       \
	} while (0)

	X(debugfs_create);
	X(async_mask_create);
#ifdef VVIDEO_RECORD
	X(recording_create);
#endif
#undef X

	if (warnings)
		seq_puts(seq, "\n");
	if (stats)
		seq_puts(seq, "Stats:");

#define X(x)                                                                   \
	do {                                                                   \
		if (fe->stats.x)                                               \
			seq_printf(seq, " " #x ":%d", fe->stats.x);            \
	} while (0)

	X(pending_releases);
	X(done_pending_releases);
	X(pending_link_ons);
	X(done_pending_link_ons);
#undef X
	if (stats)
		seq_puts(seq, "\n");

	seq_puts(seq,
		"L.Mi Pr OC VI FsMi Path--------- ON FN A RB0 SON Name\n");
	if (proc.ext)
		seq_puts(seq,
		" Sl V DMABUF-address-- FD RC SFC CFC Nd/P Ty TypeName GID\n");
	vmq_links_iterate(fe->links, vvideo_link_proc_show, &proc);

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

static int vvideo_proc_init(struct vvideo_fe_t *fe)
{
	struct proc_dir_entry *ent;

	fe->proc_normal = fe->proc_ext = fe;
	ent = proc_create_data(VVIDEO_PROC_STD, 0444, NULL, &vvideo_proc_ops,
			       &fe->proc_normal);
	if (!ent) {
		VVIDEO_ERR("Could not create /proc/" VVIDEO_PROC_STD "\n");
		return -ENOMEM;
	}
	ent = proc_create_data(VVIDEO_PROC_EXT, 0444, NULL, &vvideo_proc_ops,
			       &fe->proc_ext);
	if (!ent) {
		VVIDEO_ERR("Could not create /proc/" VVIDEO_PROC_EXT "\n");
		remove_proc_entry(VVIDEO_PROC_STD, NULL);
		return -ENOMEM;
	}
	fe->proc_exists = true;
	return 0;
}

static void vvideo_proc_exit(struct vvideo_fe_t *fe)
{
	if (fe->proc_exists) {
		remove_proc_entry(VVIDEO_PROC_STD, NULL);
		remove_proc_entry(VVIDEO_PROC_EXT, NULL);
		fe->proc_exists = false;
	}
}
#else
#define vvideo_proc_init(x)
#define vvideo_proc_exit(x)
#endif

/*----- Initialization code -----*/

static int vvideo_register_device(struct vvideo_dev_t *vvideo_dev,
				  const char *name, const int node_num)
{
	struct vvideo_fe_t *fe = vvideo_dev->link->fe;
	struct video_device *vfd;
	int ret;

	VVIDEO_LOG_VERBOSE("device %s enter, name %s node_num %d\n",
			   vvideo_dev->name, name, node_num);
	vfd = video_device_alloc();
	if (!vfd) {
		VVIDEO_ERR("device %s Failed to allocate video device\n",
			   vvideo_dev->name);
		return -ENOMEM;
	}
	vfd->fops = &vvideo_fops;
	vfd->minor = -1; /* Will be replaced with /dev/videoXXX minor */
	vfd->release = video_device_release;
	vfd->ioctl_ops = NULL;
#ifdef VVIDEO_UNUSED
	vfd->lock      = &vvideo_dev->lock;
#endif
	vfd->v4l2_dev = &fe->v4l2_dev;
	vfd->vfl_dir = VFL_DIR_M2M;
#if KERNEL_VERSION(4, 7, 0) <= LINUX_VERSION_CODE
	vfd->device_caps =  V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING |
			V4L2_CAP_VIDEO_OUTPUT_MPLANE |
			V4L2_CAP_VIDEO_CAPTURE_MPLANE;
#endif

	snprintf(vfd->name, sizeof(vfd->name), "%s", name);
	video_set_drvdata(vfd, vvideo_dev);

	VVIDEO_LOG(
	    "device %s Trying to register video device '%s' as /dev/video%d\n",
		   vvideo_dev->name, vfd->name, node_num);
	ret = video_register_device(vfd, VFL_TYPE_VIDEO, node_num);
	if (ret) {
		VVIDEO_ERR(
		"device %s Failed to register video device /dev/video%d\n",
			   vvideo_dev->name, node_num);
		video_device_release(vfd);
		return ret;
	}
	vvideo_dev->vfd = vfd;
	return 0;
}

/*
 * vvideo_dev_init() function is called to initialize a device
 * instance during driver initialization phase.
 */
static int vvideo_dev_init(struct vvideo_dev_t *vvideo_dev, unsigned int minor,
			   struct vvideo_link_t *vl)
{
	const struct vvideo_config_t *config;
	struct video_device *vfd;
	int wanted_node;
	int ret;

	VVIDEO_LOG_VERBOSE("minor %d\n", minor);
	vvideo_dev->link = vl;
	vvideo_dev->minor = minor;
	snprintf(vvideo_dev->name, sizeof(vvideo_dev->name), "%d.%d",
		 vl->number, minor);
#ifdef VVIDEO_RECORD
	vvideo_dev->records = kcalloc(VVIDEO_RECORD_MAX,
					sizeof(struct vvideo_record_t),
					      GFP_KERNEL);
	if (!vvideo_dev->records) {
		VVIDEO_ERR("device %s Out of memory for records\n",
			vvideo_dev->name);
		return -ENOMEM;
	}
#endif
	/*
	 * Initialize mutual exclusion lock
	 * and waiting queue
	 */
	mutex_init(&vvideo_dev->lock);
	/* vvideo_dev->open_count stays at zero */
	/* vvideo_dev->errors stays at zero */
	/* vvideo_dev->warnings stays at zero */
	/* vvideo_dev->stats stays at zero */
	spin_lock_init(&vvideo_dev->ctx_lock);
	/* vvideo_dev->ctx stays at zero */
	config = vvideo_dev_config(vvideo_dev);
	if (!*config->name) {
		VVIDEO_ERR("device %s Bad or missing name (%s,%d)\n",
			   vvideo_dev->name, config->name, config->orig_node);
		return -EINVAL;
	}
	vvideo_dev->async = vvideo_config_async(config);
	wanted_node = config->final_node >= 0 ? config->final_node :
						config->orig_node;
	ret = vvideo_register_device(vvideo_dev, config->name, wanted_node);
	if (ret) {
		VVIDEO_ERR(
			"device %s vvideo_register_device(%s %d) failed (%d)\n",
			vvideo_dev->name, config->name, wanted_node, ret);
		return ret;
	}
	vfd = vvideo_dev->vfd;
	if ((vfd->num != config->orig_node && config->orig_node >= 0) ||
	    vfd->num != wanted_node) {
		VVIDEO_INFO(
		    "Device '%s' got node-num %d (original is %d, wanted %d)\n",
			    vfd->name, vfd->num, config->orig_node,
			    wanted_node);
	}
	/*
	 * "vfd->index == minor" is only true when there is only one
	 * backend and only one VMQ link between frontend and backend.
	 * Indeed, vfd->index is global for all created virtual devices,
	 * while minor is per VMQ link.
	 */
	VVIDEO_INFO("device '%s' as /dev/video%d name %s link %s\n",
		vfd->name, vfd->num, vvideo_dev->name, vl->name);
	return 0;
}

/*
 * vvideo_dev_destroy() function is called to free device
 * resources and destroy a device instance during driver
 * exit phase (see vvideo_module_exit()).
 */
static void vvideo_dev_destroy(struct vvideo_dev_t *vvideo_dev)
{
	VVIDEO_LOG("device %s\n", vvideo_dev->name);
	if (vvideo_dev->vfd) {
		video_unregister_device(vvideo_dev->vfd); /* void */
		vvideo_dev->vfd = NULL;
	}
#ifdef VVIDEO_RECORD
	kfree(vvideo_dev->records);
	vvideo_dev->records = NULL;
#endif
}

/* Called from vvideo_module_cleanup() only */

static _Bool vvideo_link_free(vmq_link_t *link2, void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);

	if (vl->devs) {
		struct vvideo_dev_t *vvideo_dev;
		int vvideo_dev_slot;

		VVIDEO_FOR_ALL_DEVS(vl, vvideo_dev_slot, vvideo_dev)
		{
			vvideo_dev_destroy(vvideo_dev);
		}
		kfree(vl->devs);
		vl->devs = NULL;
	}
	/* There is nothing to free in msgs */
	kfree(vl->msgs);
	kfree(vl->configs.configs);
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	/* If vfence2_connect() fails, we come here with NULL pointers */
	if (vl->vfence2_connection)
		(void)vfence2_disconnect(vl->vfence2_connection);
#endif
	kfree(vl);
	return false; /* Keep iterating */
}

/*
 * vvideo_module_cleanup() function is called
 * to free driver resources. It is used during driver
 * initialization phase (if something goes wrong) and
 * during driver exit phase.
 *
 * In both cases the Linux driver framework guarantees
 * that there is no other activity in this driver.
 */
static void vvideo_module_cleanup(struct vvideo_fe_t *fe)
{
	struct platform_device *pdev;

	VVIDEO_LOG("enter\n");
	if (!fe)
		return;
	debugfs_remove_recursive(fe->debugfs);
	vvideo_proc_exit(fe);
	if (fe->links) {
		vmq_links_stop(fe->links);
		vmq_links_abort(fe->links);
		if (fe->work_inited)
			cancel_work_sync(&fe->work);
		vmq_links_iterate(fe->links, vvideo_link_free, fe);
		vmq_links_finish(fe->links);
		fe->links = NULL;
	}
	if (fe->v4l2_dev_registered) {
		v4l2_device_unregister(&fe->v4l2_dev); /* void */
		fe->v4l2_dev_registered = false;
	}
	/* there is no mutex destroy */
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	if (fe->vfence2_client) {
		const int ret = vfence2_destroy_client(fe->vfence2_client);

		if (ret)
			VVIDEO_WARN("vfence2_destroy_client() failed (%d)\n",
				    ret);
		fe->vfence2_client = NULL;
	}
#endif
#ifndef VVIDEO_LOCAL_GID
	if (fe->vdmaheap_client) {
		const int ret = vdmaheap_kapi_client_release(fe->vdmaheap_client);

		if (ret)
			VVIDEO_WARN("vdmaheap_kapi_client_release() failed (%d)\n",
				    ret);
		fe->vdmaheap_client = NULL;
	}
#endif
	kmem_cache_destroy(fe->poll_kmem_cache);
	fe->poll_kmem_cache = NULL;
	pdev = fe->pdev;
	platform_set_drvdata(pdev, NULL); /* Remove the "fe" pointer */
	kfree(fe);
	VVIDEO_LOG("DONE\n");
}

/* Only called from vvideo_module_init() */

static _Bool vvideo_link_init(vmq_link_t *link2, void *cookie)
{
	struct vvideo_link_t *vl = VVIDEO_LINK(link2);
	struct vvideo_fe_t *fe = (struct vvideo_fe_t *)cookie;
	struct vvideo_dev_t *vvideo_dev;
	int vvideo_dev_slot;
	struct vvideo_msg_t *m;
	int m_slot;

#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	const int ret = vfence2_connect(fe->vfence2_client,
					vmq_peer_osid(link2),
					&vl->vfence2_connection);
	if (ret) {
		VVIDEO_ERR("vfence2_connect(%d) failed (%d)\n",
			   vmq_peer_osid(link2), ret);
		return true;	/* Stop iterating */
	}
#endif
	/* Not yet vl->link_on, wait for "link_on" */
	vl->link = link2;
	vl->number = fe->link_count++;
	snprintf(vl->name, sizeof(vl->name), "%d.%d", vl->number,
		 vmq_peer_osid(link2));
	vl->fe = fe;
	/* vl->configs was initialized by vvideo_cb_get_xx_config() */
	/* vl->xx_config ditto */
	vipc_ctx_init(&vl->vipc_ctx, link2);
	if (vl->configs.count <= 0) {
		/*
		 * Should not happen, link ignored
		 * in vvideo_cb_get_xx_config()
		 */
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
	mutex_init(&vl->abort_lock);
	spin_lock_init(&vl->msg_lock);
	/*
	 * vl->open_wait must be inited before we create video devices,
	 * because V4L2 is going to immediately open them and use the
	 * queue.
	 */
	init_waitqueue_head(&vl->open_wait);
	/* Same remark for msgs */
	vl->msgs = kcalloc(vl->xx_config.msg_count, sizeof(struct vvideo_msg_t),
			GFP_KERNEL);
	if (!vl->msgs) {
		VVIDEO_ERR("link %s Out of memory\n", vl->name);
		return true; /* Stop iterating */
	}
	VVIDEO_FOR_ALL_MSGS(vl, m_slot, m)
	{
		m->slot = m_slot;
		snprintf(m->name, sizeof(m->name), "msg%d", m_slot);
		m->link = vl;
		/* State Free (I) */
		/* m->ctx remains unset for now */
		/* m->req remains unset for now */
		/* m->req_owned remains at false */
		/* m->replied remains at false */
		/* m->pid remains at 0, not set */
		/* m->async remains at 0, not set */
		/* m->ignored remains at 0, not set */
		/* m->orphaned remains at 0, not set */
		/* m->cookie_found remains at 0, not set */
		/* m->vmq remains at false */
		/* m->abort remains at false */
		/* m->kref remains at 0, no references */
		/* m->checksum remains at 0, not set */
		/* m->cookie remains at 0, not set */
	}
	VVIDEO_FOR_ALL_DEVS(vl, vvideo_dev_slot, vvideo_dev)
	{
		if (vvideo_dev_init(vvideo_dev, vvideo_dev_slot, vl))
			/* Error message already issued */
			return true; /* Stop VMQ-iterating */
	}
	fe->dev_num += vl->configs.count;
	return false; /* Keep iterating */
}

static int vvideo_pdev_probe(struct platform_device *pdev)
{
	struct vvideo_fe_t *fe;
#ifndef VVIDEO_LOCAL_GID
	struct vdmaheap_client_props props;
#endif
	int ret;

	fe = kzalloc(sizeof(*fe), GFP_KERNEL);
	if (!fe) {
		VVIDEO_ERR("failed to allocate struct vvideo_fe_t\n");
		ret = -ENOMEM;
		goto out;
	}
	fe->poll_kmem_cache = KMEM_CACHE(vvideo_poll_t, 0);
	if (!fe->poll_kmem_cache) {
		VVIDEO_ERR("could not create kmem_cache\n");
		ret = -ENOMEM;
		goto out;
	}
	fe->async_mask = VVIDEO_ASYNC_MASK_DEFAULT;
#ifdef VVIDEO_RECORD
	fe->recording = VVIDEO_RECORD_DEFAULT;
#endif
	VVIDEO_LOG_VERBOSE("allocated new fe %p", fe);
	fe->pdev = pdev;

#ifndef VVIDEO_LOCAL_GID
	ret = vdmaheap_kapi_client_create(&fe->vdmaheap_client);
	if (ret) {
		VVIDEO_ERR("vdmaheap_kapi_client_create() failed (%d)\n", ret);
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
	ret = vdmaheap_kapi_set_client_props(fe->vdmaheap_client, &props);
	if (ret) {
		VVIDEO_ERR("vdmaheap_kapi_set_client_props() failed (%d)\n", ret);
		goto out;
	}
	VVIDEO_INFO("vdmaheap cache enabled, grace delay %d ms\n",
		jiffies_to_msecs(props.grace.requested));
#endif
#if IS_ENABLED(CONFIG_VLX_VFENCE2)
	ret = vfence2_create_client(VVIDEO_DRIVER, &fe->vfence2_client);
	if (ret) {
		VVIDEO_ERR("vfence2_create_client() failed (%d)\n", ret);
		goto out;
	}
#endif
	ret = vmq_links_init_ex(&fe->links, VVIDEO2_VLINK_NAME,
				&vvideo_callbacks, NULL /*tx_config*/,
				&vvideo_rx_config, fe, true /*frontend*/);
	if (ret) {
		VVIDEO_ERR("Failed VMQ link initialization (%d)\n", ret);
		goto out;
	}
	mutex_init(&fe->mutex);
	ret = v4l2_device_register(&pdev->dev, &fe->v4l2_dev);
	if (ret) {
		VVIDEO_ERR("v4l2_device_register() failed (%d)\n", ret);
		goto out;
	}
	fe->v4l2_dev_registered = true;
	if (vmq_links_iterate(fe->links, vvideo_link_init, fe)) {
		/* Error message issued */
		ret = -EINVAL;
		goto out;
	}
	VVIDEO_LOG("dev_num %d\n", fe->dev_num);
	if (fe->dev_num <= 0) {
		VVIDEO_ERR("No V4L2 devices to manage\n");
		ret = -ENOENT;
		goto out;
	}
	INIT_WORK(&fe->work, vvideo_work);
	fe->work_inited = true;
	ret = vmq_links_start(fe->links);
	if (ret) {
		VVIDEO_ERR("VMQ start failed (%d)\n", ret);
		goto out;
	}
	vvideo_proc_init(fe);
	platform_set_drvdata(pdev, fe);

	fe->debugfs = debugfs_create_dir(VVIDEO_DRIVER, NULL /*parent*/);
	if (!fe->debugfs) {
		VVIDEO_WARN_LIMITED(
			fe->warnings.debugfs_create,
			"Failed to create /sys/kernel/debug/" VVIDEO_DRIVER
			"\n");
	} else {
		debugfs_create_u32 (VVIDEO_ASYNC_MASK, 0600, fe->debugfs,
					&fe->async_mask);
#ifdef VVIDEO_RECORD
		debugfs_create_u32 (VVIDEO_RECORDING, 0600, fe->debugfs,
					&fe->recording);
#endif
	}
	/* fe->errors stays at zero */
	/* fe->warnings stays at zero */
	VVIDEO_INFO("probe ok\n");
	return 0;

out:
	vvideo_module_cleanup(fe);
	VVIDEO_ERR("probe failed (%d)\n", ret);
	return ret;
}

static int vvideo_pdev_remove(struct platform_device *pdev)
{
	struct vvideo_fe_t *fe = platform_get_drvdata(pdev);

	VVIDEO_LOG("enter\n");
	if (!fe) {
		VVIDEO_ERR("ENODATA\n");
		return -ENODATA;
	}
	vvideo_module_cleanup(fe);
	VVIDEO_LOG("exiting\n");
	return 0;
}

static struct platform_driver vvideo_platform_driver = {
	.remove = vvideo_pdev_remove,
	.driver = {
		.name  = VVIDEO_DRIVER,
		.owner = THIS_MODULE
	}
};

/*----- Entry and exit -----*/

static struct platform_device *vvideo_pdev;

static int __init vvideo_module_init(void)
{
	int ret;

	vvideo_pdev = platform_device_register_simple(
		VVIDEO_DRIVER, PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(vvideo_pdev)) {
		ret = PTR_ERR(vvideo_pdev);
		VVIDEO_ERR("platform_device_register_simple failed (%d)\n",
			   ret);
		goto out;
	}
	/*
	 * "struct platform_device* vvideo_pdev" will be passed to
	 * vvideo_pdev_probe() and vvideo_pdev_remove().
	 */
	ret = platform_driver_probe(&vvideo_platform_driver, vvideo_pdev_probe);
	if (ret) {
		VVIDEO_ERR("platform_driver_probe() failed (%d)\n", ret);
		platform_device_unregister(vvideo_pdev); /* void */
		goto out;
	}
out:
	VVIDEO_INFO("module loaded %s\n", ret ? "with errors" : "ok");
	return ret;
}

static void __exit vvideo_module_exit(void)
{
	VVIDEO_LOG("enter\n");
	platform_driver_unregister(&vvideo_platform_driver); /* void */
	platform_device_unregister(vvideo_pdev); /* void */
	VVIDEO_INFO("module unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("virtual V4L2 frontend v2");

module_init(vvideo_module_init);
module_exit(vvideo_module_exit);
