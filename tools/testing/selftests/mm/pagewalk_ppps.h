// SPDX-License-Identifier: GPL-2.0
#ifndef __SELFTEST_MM_PAGEWALK_PPPS_H
#define __SELFTEST_MM_PAGEWALK_PPPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PAGEWALK_PPPS_DEVICE_NAME "pagewalk_ppps"

struct pagewalk_ppps_args {
	__s32 fd;
	__u32 reserved;
	__u64 offset;
	__u64 length;
	__u64 count;
	__u64 bitmap_weight;
};

#define PAGEWALK_PPPS_IOCTL_WRITE_PROTECT \
	_IOWR('w', 1, struct pagewalk_ppps_args)
#define PAGEWALK_PPPS_IOCTL_CLEAN \
	_IOWR('w', 2, struct pagewalk_ppps_args)

#endif
