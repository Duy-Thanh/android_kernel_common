/* SPDX-License-Identifier: GPL-2.0+ */
/*
 ****************************************************************
 *
 *  Component: vbufq
 *
 *  Copyright (c) 1999-2020 Red Bend. All Rights Reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License Version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 *  You should have received a copy of the GNU General Public License Version 2
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *  Contributor(s):
 *
 *    Nicolas Lavocat (nicolas.lavocat@harman.com)
 *
 ****************************************************************
 */
#ifndef __VBUFQ_COMMON_H__
#define __VBUFQ_COMMON_H__
#include <nk/nkern.h>
#include <uapi/linux/bufq.h>
#include <vlx/vdriver-lib.h>
#include "vlx-vmq.h"
#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
#include <vlx/pv-version-interface.h>

/*
 * The version of vbufq driver should be able to update without
 * dependency between VMs.
 */
#define VBUFQ_PV_VERSION		PV_DRV_VERSION(1, 0, 0)
#endif

#define VBUFQ_MAX_MSG_NB 32
typedef nku64_f vbufq_cookie_t;
#define VBUFQ_MAX_COMM_LEN 16
enum vbufq_cmd_type {
	VBUFQ_CMD_NONE = 0,
	VBUFQ_CMD_FILE_OPEN,
	VBUFQ_CMD_FILE_CLOSE,
	VBUFQ_CMD_IOCTL,
};

struct vbufq_msg_header {
	unsigned int cmd_type;
	unsigned int cmd;
	unsigned int msg_nb;
	void *be_cookie;
	void *fe_cookie;
	char name[VBUFQ_MAX_COMM_LEN];
#ifndef CONFIG_VLX_VBUFQ_DISABLE_VERSIONING
	struct pv_driver_header_v1_t pv_ver;
#endif
};

struct vbufq_msg_status {
	int ret;
	nku8_f success;
};

typedef union vbufq_payload_t {
	struct bufq_id bufq_id;
	struct bufq_attribute bufq_attribute;
	struct bufq_buf bufq_buf;
	unsigned int an_int;

} vbufq_payload_t;

typedef struct vbufq_msg_t {
	vbufq_cookie_t cookie;
	struct vbufq_msg_header header;
	vbufq_payload_t payload;
	struct vbufq_msg_status status;
} vbufq_msg_t;

/* Functions */
#define vbufq_kfree_and_clear(ptr)                                             \
	do {                                                                   \
		kfree(ptr);                                                    \
		(ptr) = NULL;                                                  \
	} while (0)

#endif // __VBUFQ_COMMON_H__
