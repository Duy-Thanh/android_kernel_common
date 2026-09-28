/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ANON_EXCLUSIVE_PROBE_PPPS_H
#define __ANON_EXCLUSIVE_PROBE_PPPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct anon_exclusive_probe_ppps {
	__u64 address;
	__u64 pfn;
	__u32 anon;
	__u32 exclusive;
	__u32 packed;
	__u32 reserved;
};

#define ANON_EXCLUSIVE_PROBE_PPPS_IOCTL \
	_IOWR(0xa5, 1, struct anon_exclusive_probe_ppps)

#endif /* __ANON_EXCLUSIVE_PROBE_PPPS_H */
