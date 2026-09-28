/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SELFTEST_MM_FRAME_VECTOR_PPPS_H
#define __SELFTEST_MM_FRAME_VECTOR_PPPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FRAME_VECTOR_PPPS_DEVICE_NAME "frame_vector_ppps"
#define FRAME_VECTOR_PPPS_MAX_FRAMES 4

struct frame_vector_ppps_args {
	__u64 address;
	__u32 nr_frames;
	__u32 capacity;
	__u32 write;
	__u32 count;
	__s64 result;
	__u32 frame_size;
	__u32 offsets[FRAME_VECTOR_PPPS_MAX_FRAMES];
};

#define FRAME_VECTOR_PPPS_IOCTL \
	_IOWR('F', 1, struct frame_vector_ppps_args)

#endif
