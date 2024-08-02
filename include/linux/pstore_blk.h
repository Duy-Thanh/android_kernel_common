/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PSTORE_BLK_H_
#define __PSTORE_BLK_H_

#include <linux/types.h>
#include <linux/pstore.h>
#include <linux/pstore_zone.h>

/**
 * struct pstore_device_info - back-end pstore/blk driver structure.
 *
 * @flags:	Refer to macro starting with PSTORE_FLAGS defined in
 *		linux/pstore.h. It means what front-ends this device support.
 *		Zero means all backends for compatible.
 * @zone:	The struct pstore_zone_info details.
 *
 */
struct pstore_device_info {
	unsigned int flags;
	struct pstore_zone_info zone;
};

int  register_pstore_device(struct pstore_device_info *dev);
void unregister_pstore_device(struct pstore_device_info *dev);

/**
 * struct pstore_blk_config - the pstore_blk backend configuration
 *
 * @device:		Name of the desired block device
 * @max_reason:		Maximum kmsg dump reason to store to block device
 * @kmsg_size:		Total size of for kmsg dumps
 * @pmsg_size:		Total size of the pmsg storage area
 * @console_size:	Total size of the console storage area
 * @ftrace_size:	Total size for ftrace logging data (for all CPUs)
 */
struct pstore_blk_config {
	char device[80];
	enum kmsg_dump_reason max_reason;
	unsigned long kmsg_size;
	unsigned long pmsg_size;
	unsigned long console_size;
	unsigned long ftrace_size;
};

/**
 * pstore_blk_get_config - get a copy of the pstore_blk backend configuration
 *
 * @info:	The sturct pstore_blk_config to be filled in
 *
 * Failure returns negative error code, and success returns 0.
 */
int pstore_blk_get_config(struct pstore_blk_config *info);

/**
 * struct ramoops_record_header_t - ramoops record header
 *
 * @pos:		Position in the block file where kmsg is dumped
 * @size:		size of kmsg to be dumped
 */
struct ramoops_record_header_t {
	uint64_t pos;
	uint64_t size;
};

/**
 * struct ramoops_record_t - ramoops record info
 *
 * @header:		ramoops record header
 * @buf:		buf at which kmsg record is dumped
 * @off:		Offset where next record is dumped in ramoops
 */
struct ramoops_record_t {
	struct ramoops_record_header_t header;
	char *buf;
};

/**
 * struct ramoops_header_t - ramoops header info
 *
 * @header:		Random magic number
 * @records:		Number of times the records dumped to ramoops region
 * @off:		Offset where next record will be dumped in ramoops
 */
struct ramoops_header_t {
	uint64_t magic;
	uint32_t dumpcnt;
	uint32_t off;
};

/**
 * struct ramoops_t - ramoops memory layout
 *
 * @header:		ramoops header info
 * @records:		ramoops kmsg records
 */
struct ramoops_t {
	struct ramoops_header_t header;
	struct ramoops_record_t *records;
};

#endif
