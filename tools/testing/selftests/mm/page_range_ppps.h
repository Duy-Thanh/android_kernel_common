/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PAGE_RANGE_PPPS_H
#define PAGE_RANGE_PPPS_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define PAGE_RANGE_PPPS_MAX 4
struct page_range_ppps_args {
	__u64 address;
	__u64 length;
	__u32 capacity;
	__u32 pin;
	__s32 result;
	__u32 helpers_ok;
	struct {
		__u32 offset;
		__u32 length;
		__u32 first;
		__u32 last;
	} spans[PAGE_RANGE_PPPS_MAX];
};

#define PAGE_RANGE_PPPS_IOCTL _IOWR('i', 0x72, struct page_range_ppps_args)

struct file_refs_ppps_args {
	__s32 fd;
	__s32 refs;
};

#define FILE_REFS_PPPS_IOCTL _IOWR('i', 0x73, struct file_refs_ppps_args)
#endif
