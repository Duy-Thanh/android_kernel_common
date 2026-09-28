/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FAULT_IN_PPPS_H
#define __FAULT_IN_PPPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define FAULT_IN_PPPS_DEVICE_NAME "fault_in_ppps"

enum fault_in_ppps_operation {
	FAULT_IN_PPPS_WRITEABLE = 0,
	FAULT_IN_PPPS_SAFE_WRITEABLE,
	FAULT_IN_PPPS_READABLE,
};

struct fault_in_ppps_args {
	__u64 address;
	__u64 length;
	__u32 operation;
	__u32 reserved;
	__u64 not_faulted;
};

#define FAULT_IN_PPPS_IOCTL \
	_IOWR('f', 0x71, struct fault_in_ppps_args)

#endif
