// SPDX-License-Identifier: GPL-2.0
#ifndef __SELFTEST_MM_GUP_RETRY_PPPS_H
#define __SELFTEST_MM_GUP_RETRY_PPPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define GUP_RETRY_PPPS_DEVICE_NAME "gup_retry_ppps"

struct gup_retry_ppps_args {
	__u64 address;
	__s64 nr_pinned;
	__u32 second_matches_expected;
	__u32 reserved;
};

#define GUP_RETRY_PPPS_IOCTL \
	_IOWR('g', 1, struct gup_retry_ppps_args)

#endif
