/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _ZRAM_IOCTL_H_
#define _ZRAM_IOCTL_H_

struct zram_ioc_data_process_writeback {
	int pidfd;
};

struct zram_ioc_data {
	union {
		struct zram_ioc_data_process_writeback process_writeback;
	} data;
};

#if IS_ENABLED(CONFIG_ZRAM_WRITEBACK)
int zram_ioctl_process_writeback_scan(struct zram *zram,
				      struct zram_ioc_data *ioc_data,
				      struct zram_pp_ctl *ctl);
#endif

#define ZRAM_IOC_MAGIC 'z'
#define ZRAM_IOC_PROCESS_WRITEBACK _IOWR(ZRAM_IOC_MAGIC, 1, struct zram_ioc_data)

#endif /* _ZRAM_IOCTL_H_ */
