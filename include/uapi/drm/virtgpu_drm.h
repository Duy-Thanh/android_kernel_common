/*
 * Copyright 2013 Red Hat
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef VIRTGPU_DRM_H
#define VIRTGPU_DRM_H

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/* Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 *
 * Do not use pointers, use __u64 instead for 32 bit / 64 bit user/kernel
 * compatibility Keep fields aligned to their size
 */

#define DRM_VIRTGPU_MAP         0x01
#define DRM_VIRTGPU_EXECBUFFER  0x02
#define DRM_VIRTGPU_GETPARAM    0x03
#define DRM_VIRTGPU_RESOURCE_CREATE 0x04
#define DRM_VIRTGPU_RESOURCE_INFO     0x05
#define DRM_VIRTGPU_TRANSFER_FROM_HOST 0x06
#define DRM_VIRTGPU_TRANSFER_TO_HOST 0x07
#define DRM_VIRTGPU_WAIT     0x08
#define DRM_VIRTGPU_GET_CAPS  0x09
#define DRM_VIRTGPU_RESOURCE_CREATE_V2 0x0a
#define DRM_VIRTGPU_EXECBUFFER_V2_REQUEST 0x0b
#define DRM_VIRTGPU_EXECBUFFER_V2_RESPONSE 0x0c





/*TODO*/
#define DRM_VIRTGPU_RESOURCE_TRANSFER_V2 0xff

#define VIRTGPU_EXECBUF_FENCE_FD_IN	0x01
#define VIRTGPU_EXECBUF_FENCE_FD_OUT	0x02
#define VIRTGPU_EXECBUF_FLAGS  (\
		VIRTGPU_EXECBUF_FENCE_FD_IN |\
		VIRTGPU_EXECBUF_FENCE_FD_OUT |\
		0)

#define VIRTGPU_MAP_CACHE_MASK      0x0f
#define VIRTGPU_MAP_CACHE_CACHED    0x01
#define VIRTGPU_MAP_CACHE_UNCACHED  0x02
#define VIRTGPU_MAP_CACHE_WC        0x03
struct drm_virtgpu_map {
	__u64 offset; /* use for mmap system call */
	__u32 handle;
	__u32 map_flags;
};

struct drm_virtgpu_execbuffer {
	__u32 flags;
	__u32 size;
	__u64 command; /* void* */
	__u64 bo_handles;
	__u32 num_bo_handles;
	__s32 fence_fd; /* in/out fence fd (see VIRTGPU_EXECBUF_FENCE_FD_IN/OUT) */
};

struct drm_virtgpu_execbuffer_v2_request {
	__u32 request_id;
	__u32 pad;
	__u32 request_size;
	__u32 response_size;
	__u64 request; /* void */
	__u64 bo_handles;
	__u32 num_bo_handles;
	__s32 fence_fd; /* in/out fence fd (see VIRTGPU_EXECBUF_FENCE_FD_IN/OUT) */
};

struct drm_virtgpu_execbuffer_v2_response {
	__u32 request_id;
	__u32 pad;
	__u32 response_size;
	__u64 response; /* void */
};

#define VIRTGPU_PARAM_3D_FEATURES 1 /* do we have 3D features in the hw */
#define VIRTGPU_PARAM_CAPSET_QUERY_FIX 2 /* do we have the capset fix */
/*
 * DRM_VIRTGPU_RESOURCE_CREATE_V2
 * DRM_VIRTGPU_RESOURCE_TRANSFER_V2
 * RESOURCE_INFO with resource flags (yes, changing the old value is legal).
 * VIRTGPU_MAP with flag flags (yes, changing the old value is legal).
 */
#define VIRTGPU_PARAM_RESOURCE_V2 3
#define VIRTGPU_PARAM_EXECBUFFER_V2 4 /* DRM_VIRTGPU_EXECBUFFER_V2_REQUEST
                                         DRM_VIRTGPU_EXECBUFFER_V2_RESPONSE */
#define VIRTGPU_PARAM_HOST_VISIBLE 5 /* Dynamically allocated host memory shared with guest */
#define VIRTGPU_PARAM_SHARED_GUEST 6  /* Guest system memory (usually -- dedicated heap?) shared with host */

struct drm_virtgpu_getparam {
	__u64 param;
	__u64 value;
};

/* NO_BO flags? NO resource flag? */
/* resource flag for y_0_top */
struct drm_virtgpu_resource_create {
	__u32 target;
	__u32 format;
	__u32 bind;
	__u32 width;
	__u32 height;
	__u32 depth;
	__u32 array_size;
	__u32 last_level;
	__u32 nr_samples;
	__u32 flags;
	__u32 bo_handle; /* if this is set - recreate a new resource attached to this bo ? */
	__u32 res_handle;  /* returned by kernel */
	__u32 size;        /* validate transfer in the host */
	__u32 stride;      /* validate transfer in the host */
};

struct drm_virtgpu_resource_info {
	__u32 bo_handle;
	__u32 res_handle;
	__u32 size;
	__u32 resource_flags;
};

struct drm_virtgpu_3d_box {
	__u32 x;
	__u32 y;
	__u32 z;
	__u32 w;
	__u32 h;
	__u32 d;
};

struct drm_virtgpu_3d_transfer_to_host {
	__u32 bo_handle;
	struct drm_virtgpu_3d_box box;
	__u32 level;
	__u32 offset;
};

struct drm_virtgpu_3d_transfer_from_host {
	__u32 bo_handle;
	struct drm_virtgpu_3d_box box;
	__u32 level;
	__u32 offset;
};

#define VIRTGPU_WAIT_NOWAIT 1 /* like it */
struct drm_virtgpu_3d_wait {
	__u32 handle; /* 0 is an invalid handle */
	__u32 flags;
};

struct drm_virtgpu_get_caps {
	__u32 cap_set_id;
	__u32 cap_set_ver;
	__u64 addr;
	__u32 size;
	__u32 pad;
};

struct drm_virtgpu_resource_create_v2 {
#define VIRTGPU_RESOURCE_TYPE_MASK       0x000f
#define VIRTGPU_RESOURCE_TYPE_DEFAULT_V1 0x0001
#define VIRTGPU_RESOURCE_TYPE_DEFAULT_V2 0x0002
#define VIRTGPU_RESOURCE_TYPE_HOST       0x0003
#define VIRTGPU_RESOURCE_TYPE_GUEST      0x0004
/*
 * Error cases:
 * HOST_VISIBLE_BIT without VIRTGPU_RESOURCE_TYPE_HOST
 * VIRTGPU_RESOURCE_GUEST_SHARED_BIT without VIRTGPU_RESOURCE_TYPE_GUEST
 */
#define VIRTGPU_RESOURCE_HOST_MASK             0x00f0
#define VIRTGPU_RESOURCE_HOST_VISIBLE_BIT      0x0010
#define VIRTGPU_RESOURCE_HOST_MAP_DIRECTLY_BIT 0x0020

#define VIRTGPU_RESOURCE_GUEST_MASK                  0x0f00
#define VIRTGPU_RESOURCE_GUEST_SHARED_BIT            0x0100
#define VIRTGPU_RESOURCE_GUEST_EMULATED_COHERENT_BIT 0x0200
/*
 * VIRTGPU_RESOURCE_SHAREABLE_BIT - host resource *can* be exported as an fd.
 */
#define VIRTGPU_RESOURCE_SHARE_MASK    0xf000
#define VIRTGPU_RESOURCE_SHAREABLE_BIT 0x1000
	__u32 flags;
	__u32 args_size;
	__u64 size;
	__u32 bo_handle;
	__u32 res_handle;
	__u64 args;
};

struct drm_virtgpu_resource_transfer_v2 {
	__u32 bo_handle;
#define VIRTGPU_TRANSFER_TO_HOST   0x0001
#define VIRTGPU_TRANSFER_FROM_HOST 0x0002
	__u32 flags;
	__u32 count;
	__u64 offsets;        /* u64* */
	__u64 ranges;         /* u64* */
	__s32 fence_fd;       /* out */
};

#define DRM_IOCTL_VIRTGPU_MAP \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_MAP, struct drm_virtgpu_map)

#define DRM_IOCTL_VIRTGPU_EXECBUFFER \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_EXECBUFFER,\
		struct drm_virtgpu_execbuffer)

#define DRM_IOCTL_VIRTGPU_GETPARAM \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_GETPARAM,\
		struct drm_virtgpu_getparam)

#define DRM_IOCTL_VIRTGPU_RESOURCE_CREATE			\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_CREATE,	\
		struct drm_virtgpu_resource_create)

#define DRM_IOCTL_VIRTGPU_RESOURCE_INFO \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_INFO, \
		 struct drm_virtgpu_resource_info)

#define DRM_IOCTL_VIRTGPU_TRANSFER_FROM_HOST \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_TRANSFER_FROM_HOST,	\
		struct drm_virtgpu_3d_transfer_from_host)

#define DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_TRANSFER_TO_HOST,	\
		struct drm_virtgpu_3d_transfer_to_host)

#define DRM_IOCTL_VIRTGPU_WAIT				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_WAIT,	\
		struct drm_virtgpu_3d_wait)

#define DRM_IOCTL_VIRTGPU_GET_CAPS \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_GET_CAPS, \
	struct drm_virtgpu_get_caps)

#define DRM_IOCTL_VIRTGPU_RESOURCE_CREATE_V2				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_CREATE_V2,	\
		struct drm_virtgpu_resource_create_v2)

#define DRM_IOCTL_VIRTGPU_RESOURCE_TRANSFER_V2				\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_RESOURCE_TRANSFER_V2,	\
		 struct drm_virtgpu_resource_transfer_v2)

#define DRM_IOCTL_VIRTGPU_EXECBUFFER_V2_REQUEST			\
	DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_EXECBUFFER_V2_REQUEST,	\
		 struct drm_virtgpu_execbuffer_v2_request)

#define DRM_IOCTL_VIRTGPU_EXECBUFFER_V2_RESPONSE			\
        DRM_IOWR(DRM_COMMAND_BASE + DRM_VIRTGPU_EXECBUFFER_V2_RESPONSE, \
		 struct drm_virtgpu_execbuffer_v2_response)

#if defined(__cplusplus)
}
#endif

#endif
