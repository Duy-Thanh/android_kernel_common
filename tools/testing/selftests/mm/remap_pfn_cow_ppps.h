/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __REMAP_PFN_COW_PPPS_H
#define __REMAP_PFN_COW_PPPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct remap_pfn_cow_ppps_probe {
	__u64 address;
	__u32 slice;
	__u32 offset_matches;
};

#define REMAP_PFN_COW_PPPS_IOCTL \
	_IOWR(0xa5, 2, struct remap_pfn_cow_ppps_probe)

#endif /* __REMAP_PFN_COW_PPPS_H */
