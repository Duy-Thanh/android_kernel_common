// SPDX-License-Identifier: GPL-2.0+
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

#include "vlx-vipc.h"
#include "vbufq_common.h"
#include <uapi/linux/bufq.h>
#include <linux/string.h>

#define VBUFQ_PRINT_CMD(x)                                                     \
	do {								       \
		if (cmd == x)                                                  \
			return #x;					       \
	} while (0);
#define VBUFQ_LINK_DEFAULT_MSG_COUNT 32
#define VBUFQ_DEFAULT_DEV_NAME "vbufq-fe"
#define VBUFQ_LINK_BE_2_FE_MSG_COUNT 1

/**
 * @brief used to configure ring size for communication from BE to FE.
 * since today there is not communication in this direction, let's set the
 * minimum size
 */
static const vmq_xx_config_t vbufq_rx_config_be2fe = {
	.msg_count = VBUFQ_LINK_BE_2_FE_MSG_COUNT,
	.msg_max = sizeof(struct vbufq_msg_t),
	.data_count = 0,
	.data_max = 0,
	.pdev_level = VMQ_PDEV_NONE,
};

/**
 * @brief helper permitting to get an ioctl cmd in a string format, for
 * logging purpose.
 *
 * @param[in] - cmd ioctl cmd to be printed
 *
 * @return string related to the ioctl input cmd.
 */
static const char *vbufq_cmd2name(unsigned int cmd)
{
	VBUFQ_PRINT_CMD(BQ_CREATE_PROD)
	VBUFQ_PRINT_CMD(BQ_DESTROY_PROD)
	VBUFQ_PRINT_CMD(BQ_CREATE_CONS)
	VBUFQ_PRINT_CMD(BQ_DESTROY_CONS)
	VBUFQ_PRINT_CMD(BQ_SET_BUF_ATTR)
	VBUFQ_PRINT_CMD(BQ_GET_BUF_ATTR)
	VBUFQ_PRINT_CMD(BQ_QUERY_BUF)
	VBUFQ_PRINT_CMD(BQ_BUF_ALLOC)
	VBUFQ_PRINT_CMD(BQ_BUF_FREE)
	VBUFQ_PRINT_CMD(BQ_BUF_ATTACH)
	VBUFQ_PRINT_CMD(BQ_BUF_DETACH)
	VBUFQ_PRINT_CMD(BQ_BUF_DEQUEUE)
	VBUFQ_PRINT_CMD(BQ_BUF_ENQUEUE)
	VBUFQ_PRINT_CMD(BQ_BUF_ACQUIRE)
	VBUFQ_PRINT_CMD(BQ_BUF_RELEASE)
	VBUFQ_PRINT_CMD(BQ_READY_BUFS)
	VBUFQ_PRINT_CMD(BQ_BUF_LOCK)
	VBUFQ_PRINT_CMD(BQ_BUF_UNLOCK)
	return "BQ_INVALID_IOC_";
}

/**
 * @brief helper printing a error message and returning a NULL code error.
 *
 * @param[in] opt - the option raising an error.
 *
 * @return NULL
 */
static void *__init vbufq_vlink_syntax(const char *opt)
{
	ETRACE("Syntax error near '%s'\n", opt);
	return NULL;
}

static unsigned __init vbufq_get_msb_pos(unsigned long value)
{
	unsigned bitnb;

	for (bitnb = sizeof(value) * 8; bitnb;) {
		--bitnb;
		if (value & (1UL << bitnb))
			return bitnb;
	}
	return 0;
}

/**
 * @brief check, depending on if the considered info field is read
 * by a frontend or a backend if the vlink has to be ignored
 *
 *  only used by @ref vbufq_cb_get_xx_config
 *  - if the vlink is considered by a frontend, the tx info field is supposed
 *  to contain the "be," sub string
 *
 *  - if the vlink is considered by a backend, the rx info field is supposed
 *  to contain the "be," sub string
 *
 * @param [in] start - string provided by vbufq_cb_get_xx_config, containing the info
 * field of the considered vlink
 * @param [in] rx_s_info - string containing info field of one side of the
 * definition of the vlink. In case of a frontend, start == tx_s_info
 * @param [in] tx_s_info - same than rx_s_info. In case of a backend, start == rx_s_info
 *
 * @return true if the vlink as to be considered, false otherwise
 */
static bool config_is_relevant(const char *start, const char *rx_s_info,
			       const char *tx_s_info)
{
	bool is_tx_end_point = (start == tx_s_info);
	const char *supposed_to_contain_be =
		(is_tx_end_point) ? tx_s_info : rx_s_info;

	return strstr(supposed_to_contain_be, "be,");
}

/**
 * @brief check if the "be" pattern is present in the info field of a vlink
 *
 * only called by vbufq_cb_get_xx_config
 *
 * @param [in,out] start - contains the info field to be parsed
 *
 * @return true if the marker is present, 0 otherwise
 */
static bool vbufq_be_marker_is_present(const char **start)
{
	bool ret = true;
	if ((*start)[0] == 'b' && (*start)[1] == 'e') {
		(*start) += 3;
		DTRACE("backend side of vlink\n");
	} else {
		ETRACE("no be marker detected\n");
		ret = false;
	}
	return ret;
}

/**
 * @brief extract msg count of the info field of the link
 *
 * must not be called in another place than vbufq_cb_get_xx_config
 * @param [in,out] - start string to be parsed
 *
 * @return msg_count in the string (Round down to highest power of 2)
 * if the parsing was successful, -1 if a parsing error happened,
 * VBUFQ_LINK_DEFAULT_MSG_COUNT if the parsed value is negative.
 */
static long vbufq_extract_msg_count(const char **start)
{
	long msg_count = VBUFQ_LINK_DEFAULT_MSG_COUNT;
	if (**start) {
		char *end;

		msg_count = simple_strtoul(*start, &end, 0);
		if (end == *start) {
			return -1;
		}
		*start = end;
	}
	if (msg_count > 0) {
		/*
         * Round down to highest power of 2.
         * nkops.nk_mask2bit() scans from LSB,
         * so it is not suitable here.
         */
		msg_count = 1UL << vbufq_get_msb_pos(msg_count);
	} else {
		ETRACE("wrong msg_count value (%ld), default value (%d) will be used\n",
		       msg_count, VBUFQ_LINK_DEFAULT_MSG_COUNT);
		msg_count = VBUFQ_LINK_DEFAULT_MSG_COUNT;
	}
	DTRACE("message_count = %ld\n", msg_count);
	return msg_count;
}

/**
 * @brief extract the name of the node that will permit to reach the device
 *
 * Must not be called in another place than vbufq_cb_get_xx_config
 * @param [in, out] -  start string to be parsed.
 *
 * @return name of the device if properly parsed, default one otherwise.
 */

static char *vbufq_extract_dev_name(const char **start)
{
	char *ret;
	if ((**start) == ',') {
		(*start) += 1;
		ret = kstrdup(*start, GFP_KERNEL);
	} else {
		ETRACE("invalid node name, default one will be used\n");
		ret = kstrdup(VBUFQ_DEFAULT_DEV_NAME, GFP_KERNEL);
	}
	return ret;
}
/**
 * @brief VMQ configuration callback.
 * this callback is used :
 *  - on FE side for transmission configuration
 *  - on BE side for reception configuration
 *
 *  following operations are done:
 *  - check if the current vlink is to be considered by the FE/BE.
 *  indeed, since there is no notion of BE/FE in bidirectional vlinks,
 *  an additional information have to be added. This is useful mainly in the
 *  case where BE/FE have to cohabit in the same VM.
 *  If the info field in the current vlink contain the "be" tag, then it has to
 *  be considered only by the BE.
 *  Please notice that BE and FE will read at the same location the
 *  configuration data, i.e in the be side of the vlink.
 *
 *  - get msg_count
 *  - get the name of the node to be created by the FE, that will permit to
 *    reach the FE. The name is copied in the proper vbufq_link_t struct.
 *  - send back parsed configuration to VMQ.
 *
 * @remark Remaining configuration is made thanks to the static configuration
 * defined in @ref vbufq_rx_config_be2fe
 *
 * @param[in]  link - link to be configured
 * @param[in] start - content of the "info" present in dts related to the current
 * vlink
 *
 * @return a vmq_xx_config_t structure if OK, NULL otherwise
 */
static const vmq_xx_config_t *__init vbufq_cb_get_xx_config(vmq_link_t *link,
							    const char *start)
{
	vbufq_link_t *vbufq_link;
	const char *rx_s_info;
	const char *tx_s_info;
	long msg_count = VBUFQ_LINK_DEFAULT_MSG_COUNT;

	rx_s_info = vmq_link_rx_s_info(link);
	tx_s_info = vmq_link_tx_s_info(link);
	DTRACE(" called, read strings:\n"
	       " start              = %s\n"
	       " vmq_link_tx_s_info = %s\n"
	       " vmq_link_rx_s_info = %s\n",
	       (start) ? start : "NULL", (tx_s_info) ? tx_s_info : "NULL",
	       (rx_s_info) ? rx_s_info : "NULL");

	if (!start || !config_is_relevant(start, rx_s_info, tx_s_info)) {
		DTRACE("ignoring link\n");
		return VMQ_XX_CONFIG_IGNORE_VLINK;
	}

	vbufq_link = (vbufq_link_t *)kzalloc(sizeof *vbufq_link, GFP_KERNEL);

	if (vbufq_link == NULL) {
		ETRACE("error, during vbufq memory allocation\n");
		return (vmq_xx_config_t *)NULL;
	}

	VBUFQ_LINK(link) = vbufq_link;

	//formats for a back end:  be,msg_count,fe_node_name
	DTRACE("Using params '%s'\n", start);

	if (!vbufq_be_marker_is_present(&start)) {
		return (vmq_xx_config_t *)NULL;
	}

	msg_count = vbufq_extract_msg_count(&start);
	if (msg_count < 0) {
		return (vmq_xx_config_t *)vbufq_vlink_syntax(start);
	}

	vbufq_link->device_name = vbufq_extract_dev_name(&start);
	if (IS_ERR_OR_NULL(vbufq_link->device_name)) {
		ETRACE("error, unable to allocate memory for device name\n");
		return (vmq_xx_config_t *)NULL;
	}

	vbufq_link->xx_config.msg_count = msg_count;
	vbufq_link->xx_config.msg_max = sizeof(struct vbufq_msg_t);
	vbufq_link->xx_config.data_count = 0;
	vbufq_link->xx_config.data_max = 0;
	vbufq_link->xx_config.pdev_level = VMQ_PDEV_NONE;

	return &vbufq_link->xx_config;
}
