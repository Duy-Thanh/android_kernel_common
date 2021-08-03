/* SPDX-License-Identifier: GPL-2.0+ */
/*
 ****************************************************************
 *
 *  Component: VLX virtual video driver
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
 *    Adam Mirowski (adam.mirowski@harman.com)
 *
 ****************************************************************
 */

#ifndef VVIDEO_UTIL_H
#define VVIDEO_UTIL_H

/*----- System includes -----*/

#include <linux/version.h>
#include <asm/ioctl.h>
#include <linux/videodev2.h>
#include <linux/kref.h>
#include <linux/fs.h>

#if IS_ENABLED(CONFIG_ARCH_EXYNOS)
#include <uapi/linux/videodev2_exynos_media.h>
#else
#ifndef VLX_CMOCK
#pragma message "Building without specific Exynos support"
#endif
#endif

/*----- Kernel version compatibility definitions -----*/

static inline unsigned int vvideo_kref_read(const struct kref *kref)
{
#if KERNEL_VERSION(3, 19, 0) > LINUX_VERSION_CODE
	return atomic_read(&kref->refcount);
#else
	return kref_read(kref);
#endif
}

#ifdef VLX_CMOCK
int vvideo_kernel_read(struct file *file, char *buf, unsigned long count,
		       loff_t offset);
#else
static inline int vvideo_kernel_read(struct file *file, char *buf,
				     unsigned long count, loff_t offset)
{
#if KERNEL_VERSION(4, 14, 0) > LINUX_VERSION_CODE
	return kernel_read(file, offset, buf, count);
#else
	return kernel_read(file, buf, count, &offset);
#endif
}
#endif

/*----- Options -----*/

// #define VVIDEO_LOCAL_GID

/*----- Dependencies -----*/

#if !IS_ENABLED(CONFIG_VLX_VDMAHEAP)
#ifndef VLX_CMOCK
#pragma message "Compiling without VDMAHEAP, only same-VM operations possible"
#endif
#define VVIDEO_LOCAL_GID
#endif

/*----- Local includes -----*/

#ifndef VVIDEO_LOCAL_GID
#include <vlx/vdmaheap/uapi/vdmaheap_uapi.h>
#endif

/*----- Cookie-based and generic calls -----*/

typedef void (*vvideo_printk_t)(void *ctx, const char *fmt, ...);

const char *vvideo_ioctl_name_ex(unsigned long ioc, _Bool nr_only);
const char *vvideo_ioctl_name(unsigned long ioc);
void vvideo_fourcc_cookie(vvideo_printk_t pr, void *cookie, const char *legend,
			  __u32 f);
void vvideo_pixelformat_cookie(vvideo_printk_t pr, void *c, __u32 f);
const char *vvideo_v4l2_colorspace(const enum v4l2_colorspace);
const char *vvideo_v4l2_field(const enum v4l2_field);
const char *vvideo_v4l2_buf_type(const enum v4l2_buf_type);
const char *vvideo_v4l2_buf_type_short(const enum v4l2_buf_type);
void vvideo_v4l2_buffer_flags(char *buf, size_t bufsize, __u32 f);
const char *vvideo_v4l2_memory(const enum v4l2_memory);
void vvideo_v4l2_buffer_cookie(vvideo_printk_t pr, void *cookie,
			       const struct v4l2_buffer *b);
const char *vvideo_v4l2_cid(const __u32 cid);
void vvideo_v4l2_control_cookie(vvideo_printk_t pr, void *cookie,
				const struct v4l2_control *c);
void vvideo_v4l2_requestbuffers_cookie(vvideo_printk_t pr, void *cookie,
				       const struct v4l2_requestbuffers *rb);
void vvideo_v4l2_capability_cookie(vvideo_printk_t pr, void *cookie,
				   const struct v4l2_capability *c);
void vvideo_v4l2_format_cookie(vvideo_printk_t pr, void *cookie,
			       const struct v4l2_format *f);
void vvideo_v4l2_crop_cookie(vvideo_printk_t pr, void *cookie,
			     const struct v4l2_crop *c);
void vvideo_v4l2_queryctrl_cookie(vvideo_printk_t pr, void *cookie,
				  const struct v4l2_queryctrl *q);
void vvideo_v4l2_fmtdesc_cookie(vvideo_printk_t pr, void *cookie,
				const struct v4l2_fmtdesc *f);

/*----- seq_file calls -----*/

void vvideo_fourcc(struct seq_file *seq, __u32 v);
void vvideo_capabilities(struct seq_file *seq, __u32 v);
void vvideo_v4l2_capability(struct seq_file *seq,
			    const struct v4l2_capability *c);
void vvideo_v4l2_pix_format(struct seq_file *seq,
			    const struct v4l2_pix_format *p);
void vvideo_v4l2_window(struct seq_file *seq, const struct v4l2_window *w);
void vvideo_v4l2_rect(struct seq_file *seq, const struct v4l2_rect *r);
#ifdef V4L2_TYPE_IS_MULTIPLANAR
void vvideo_v4l2_plane(struct seq_file *seq, const __u32 memory,
		       const struct v4l2_plane *p);
#endif
void vvideo_v4l2_buffer(struct seq_file *seq, const struct v4l2_buffer *b);

/*----- Driver helpers -----*/

void vvideo_printk(void *ctx, const char *fmt, ...);
char vvideo_link_state(const int state);

enum vvideo_async_mode {
	VVIDEO_ASYNC_MODE_DEFAULT = -1,
	VVIDEO_ASYNC_MODE_NO,
	VVIDEO_ASYNC_MODE_YES
};

struct vvideo_config_t {
	char name[64];
	int orig_node;
	int final_node;
	enum vvideo_async_mode	async_mode;
};

struct vvideo_configs_t {
	unsigned int count;
	struct vvideo_config_t *configs;
};

int vvideo_config_parse(const char *c_info, struct vvideo_config_t *c);

/* sadk9-sys-linux/drivers/media/platform/exynos/mfc/exynos_mfc_media.h */
#define V4L2_CID_MPEG_MFC_BASE (V4L2_CTRL_CLASS_MPEG | 0x2000)
#define V4L2_CID_MPEG_MFC51_VIDEO_FRAME_TAG (V4L2_CID_MPEG_MFC_BASE + 6)
#define V4L2_CID_MPEG_MFC51_VIDEO_CHECK_STATE (V4L2_CID_MPEG_MFC_BASE + 13)
#define V4L2_CID_MPEG_MFC51_VIDEO_DISPLAY_STATUS (V4L2_CID_MPEG_MFC_BASE + 14)
#define V4L2_CID_MPEG_MFC51_VIDEO_FRAME_POC (V4L2_CID_MPEG_MFC_BASE + 29)
#define V4L2_CID_MPEG_VIDEO_SRC_BUF_FLAG (V4L2_CID_MPEG_MFC_BASE + 30)
#define V4L2_CID_MPEG_VIDEO_DST_BUF_FLAG (V4L2_CID_MPEG_MFC_BASE + 31)
#define V4L2_CID_MPEG_VIDEO_GDC_VOTF (V4L2_CID_MPEG_MFC_BASE + 32)
#define V4L2_CID_MPEG_VIDEO_FRAME_ERROR_TYPE (V4L2_CID_MPEG_MFC_BASE + 33)
#define V4L2_CID_MPEG_VIDEO_GOP_CTRL (V4L2_CID_MPEG_MFC_BASE + 35)
#define V4L2_CID_MPEG_VIDEO_GET_DISPLAY_DELAY (V4L2_CID_MPEG_MFC_BASE + 40)
#define V4L2_CID_MPEG_MFC51_VIDEO_FRAME_RATE (V4L2_CID_MPEG_MFC_BASE + 45)
#define V4L2_CID_MPEG_VIDEO_QOS_RATIO (V4L2_CID_MPEG_MFC_BASE + 50)
#define V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_CH \
	(V4L2_CID_MPEG_MFC_BASE + 52)
#define V4L2_CID_MPEG_VIDEO_VP8_HIERARCHICAL_CODING_LAYER_CH \
	(V4L2_CID_MPEG_MFC_BASE + 79)
#define V4L2_CID_MPEG_MFC_GET_VERSION_INFO (V4L2_CID_MPEG_MFC_BASE + 91)
#define V4L2_CID_MPEG_MFC_GET_EXTRA_BUFFER_SIZE (V4L2_CID_MPEG_MFC_BASE + 92)
#define V4L2_CID_MPEG_MFC_SET_DYNAMIC_DPB_MODE (V4L2_CID_MPEG_MFC_BASE + 95)
#define V4L2_CID_MPEG_MFC_SET_USER_SHARED_HANDLE (V4L2_CID_MPEG_MFC_BASE + 96)
#define V4L2_CID_MPEG_MFC_GET_EXT_INFO (V4L2_CID_MPEG_MFC_BASE + 97)
#define V4L2_CID_MPEG_MFC_GET_10BIT_INFO (V4L2_CID_MPEG_MFC_BASE + 99)
#define V4L2_CID_MPEG_MFC_GET_DRIVER_INFO (V4L2_CID_MPEG_MFC_BASE + 107)
#define V4L2_CID_MPEG_MFC_HDR_USER_SHARED_HANDLE (V4L2_CID_MPEG_MFC_BASE + 109)
#define V4L2_CID_MPEG_VIDEO_HEVC_PREPEND_SPSPPS_TO_IDR	\
	(V4L2_CID_MPEG_MFC_BASE + 153)
#define V4L2_CID_MPEG_VIDEO_HEVC_HIERARCHICAL_CODING_LAYER_CH \
	(V4L2_CID_MPEG_MFC_BASE + 154)
#define V4L2_CID_MPEG_VIDEO_VP9_HIERARCHICAL_CODING_LAYER_CH \
	(V4L2_CID_MPEG_MFC_BASE + 175)
#define V4L2_CID_MPEG_VIDEO_SKIP_LAZY_UNMAP (V4L2_CID_MPEG_MFC_BASE + 185)
#define V4L2_CID_MPEG_VIDEO_ROI_CONTROL (V4L2_CID_MPEG_MFC_BASE + 190)
#define V4L2_CID_MPEG_VIDEO_HIERARCHICAL_BITRATE_CTRL                          \
	(V4L2_CID_MPEG_MFC_BASE + 199)
#define V4L2_CID_MPEG_VIDEO_DECODING_ORDER (V4L2_CID_MPEG_MFC_BASE + 200)
#define V4L2_CID_MPEG_MFC_AV1_FILM_GRAIN_USER_SHARED_HANDLE                    \
	(V4L2_CID_MPEG_MFC_BASE + 240)

#ifdef V4L2_CID_EXYNOS_BASE
/* From drivers/media/platform/exynos/scaler/scaler.h */
#define SC_CID_FRAMERATE (V4L2_CID_EXYNOS_BASE + 110)

/* for scaler context priority setting and getting */
#define SC_CID_CTX_PRIORITY_SETTING (V4L2_CID_EXYNOS_BASE + 310)

enum {
	SC_CTX_DEFAULT_PRIO,
	SC_CTX_LOW_PRIO = SC_CTX_DEFAULT_PRIO,
	SC_CTX_HIGH_PRIO
};
#endif

struct vvideo_dmabuf_gid_t {
#ifdef VVIDEO_LOCAL_GID
	unsigned long dma_buf;
#else
	vdmaheap_buffer_gid_t gid;
	uuid_t cred;
#endif
};

#ifdef VVIDEO_LOCAL_GID
#define VVIDEO_GID_VALID(_gid)	((_gid)->dma_buf != 0)
#else
#define VVIDEO_GID_VALID(_gid)	((_gid)->gid != 0)
#endif

#define VVIDEO_CTRL_TO_GID(_c) ((struct vvideo_dmabuf_gid_t *)((_c) + 1))
#define VVIDEO_GID_STRING 40 /* 37+1 chars is the standard length */

const char *vvideo_dmabuf_gid_format(const struct vvideo_dmabuf_gid_t *gid,
				     char *buf, size_t bufsize);
_Bool vvideo_dmabuf_gid_same(const struct vvideo_dmabuf_gid_t *g1,
			     const struct vvideo_dmabuf_gid_t *g2);

#define VVIDEO_STATIC_ASSERT(x) extern char vvideo_static_assert[(x) ? 1 : -1]

/*
 * These macros can be shared between frontend and backend
 * though the data structures they operate on are not identical.
 * VVIDEO_FOR_ALL_CTXS() cannot be shared because context
 * management is different.
 */
#define VVIDEO_FOR_ALL_DEVS(_link, _i, _vvideo_dev)                            \
	for ((_i) = 0, (_vvideo_dev) = (_link)->devs;                          \
	     (_i) < (_link)->configs.count; ++(_i), ++(_vvideo_dev))

#define VVIDEO_FOR_ALL_DMABUFS(_ctx, _i, _d)                                   \
	for ((_i) = 0, (_d) = (_ctx)->dma_bufs; (_i) < VVIDEO_DMABUF_MAX;      \
	     ++(_i), ++(_d))

#define VVIDEO_FOR_ALL_DMABUFS_REVERSE(_ctx, _d) \
	for ((_d##_counter) = 0, (_d) = (_ctx)->dma_bufs + \
	     (_ctx)->dma_buf_last % VVIDEO_DMABUF_MAX; \
	     (_d##_counter) < VVIDEO_DMABUF_MAX; ++(_d##_counter), \
	     (_d) > (_ctx)->dma_bufs ? --(_d) : \
	     ((_d) = (_ctx)->dma_bufs + VVIDEO_DMABUF_MAX - 1))

#endif /* VVIDEO_UTIL_H */
