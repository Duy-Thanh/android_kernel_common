/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MMU_NOTIFIER_PPPS_H
#define MMU_NOTIFIER_PPPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct mmu_notifier_ppps_range {
	__u64 start;
	__u64 end;
	__u64 page_size;
};

struct mmu_notifier_ppps_stats {
	__u64 total_callbacks;
	__u64 callbacks;
	__u64 bad_ranges;
	__u64 last_start;
	__u64 last_end;
	__u64 max_span;
};

#define MMU_NOTIFIER_PPPS_IOC_MAGIC	'N'
#define MMU_NOTIFIER_PPPS_SET_RANGE	_IOW(MMU_NOTIFIER_PPPS_IOC_MAGIC, 0, \
					     struct mmu_notifier_ppps_range)
#define MMU_NOTIFIER_PPPS_GET_STATS	_IOR(MMU_NOTIFIER_PPPS_IOC_MAGIC, 1, \
					     struct mmu_notifier_ppps_stats)

#endif
