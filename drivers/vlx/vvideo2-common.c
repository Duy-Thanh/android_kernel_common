// SPDX-License-Identifier: GPL-2.0+
/*
 ****************************************************************
 *
 *  Component: VLX virtual video v2 common code
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
 *    Adam Mirowski (adam.mirowski@harman.com)
 *
 ****************************************************************
 */

/*----- vlink c_info/s_info parsing -----*/

static void __init vvideo_syntax(const char *opt)
{
	VVIDEO_ERR("Syntax error near '%s'\n", opt);
}

static inline _Bool __init vvideo_vvideo2_end(const char ch)
{
	return ch == ')' || ch == ';';
}

/*
 * Returns 1 on success and 0 on failure.
 */

static _Bool __init vvideo_vvideo2_one(struct vvideo_configs_t *c,
				       const char *start)
{
	struct vvideo_config_t *new;

	if (*start != '(') {
		vvideo_syntax(start);
		return 0;
	}
	++start;
	new = krealloc(c->configs, (c->count + 1) * sizeof(*c->configs),
		       GFP_KERNEL);
	if (!new) {
		VVIDEO_ERR("Out of memory for config '%s' minor %d\n", start,
			   c->count);
		return 0;
	}
	c->configs = new;

	if (vvideo_config_parse(start, c->configs + c->count)) {
		VVIDEO_ERR("Failed to parse config '%s' minor %d\n", start,
			   c->count);
		return 0;
	}
	++c->count;
	return 1;
}

static unsigned int vvideo_next_log2(unsigned long x)
{
	return x <= 1 ? 0 : 8 * sizeof(unsigned long) - __builtin_clzl(x - 1);
}

static unsigned long vvideo_next_pow2(unsigned long x)
{
	return 1 << vvideo_next_log2(x);
}

/*
 * Each link can have a custom communications configuration,
 * depending on the vlink parameter indicated by the "start"
 * string. To store the resulting various vmq_xx_config_t we
 * use the private data of the vmq_link_t (struct vvideo_link_t), which
 * must therefore be allocated here.
 * All the other initializations of struct vvideo_link_t are however
 * performed later, by vvideo_link_init(), to not overload this
 * function.
 */

static const vmq_xx_config_t *__init vvideo_cb_get_xx_config(vmq_link_t *link2,
							     const char *start)
{
	struct vvideo_link_t *vl = kzalloc(sizeof(*vl), GFP_KERNEL);
	const char *saved_start;
	long msg_count;

	if (!vl) {
		VVIDEO_ERR("out of memory for link\n");
		return NULL;
	}
	VVIDEO_LINK(link2) = vl;
	/*
	 * Frontend vlink endpoint configuration:
	 *  info = "fe[,<elems>] vvideo2=(...) [vvideo2=(...)]";
	 * Backend vlink endpoint configuration:
	 *  info = "be";
	 * Backend  driver gets start == rx_s_info.
	 * Frontend driver gets start == tx_s_info.
	 */
	if (!start || start == vmq_link_tx_s_info(link2))
		/* We are in frontend */
		start = vmq_link_rx_s_info(link2);
	else if (start == vmq_link_rx_s_info(link2))
		/* We are in backend */
		start = vmq_link_tx_s_info(link2);
	saved_start = start;
	VVIDEO_LOG("vmq_link_tx_s_info='%s'\n", vmq_link_tx_s_info(link2));
	VVIDEO_LOG("vmq_link_rx_s_info='%s'\n", vmq_link_rx_s_info(link2));

	/* Parse vvideo2=(...) parameters into array, compute their count */
	while (start && (start = strstr(start, "vvideo2=")) != 0) {
		start += 8;
		if (!vvideo_vvideo2_one(&vl->configs, start)) {
			/* Error message already issued */
			return NULL;
		}
	}
	if (!vl->configs.count) {
		VVIDEO_WARN("No vvideo2 devices on vlink VM%d<-VM%d\n",
			    vmq_local_osid(link2), vmq_peer_osid(link2));
		return VMQ_XX_CONFIG_IGNORE_VLINK;
	}
	/* +1 is for abort message */
	msg_count = vl->configs.count * VVIDEO2_DEV_AVERAGE_REQS + 1;

	start = saved_start;
	/* Found some configs, so start is non-NULL and non-empty */
	VVIDEO_LOG("Using params '%s'\n", start);
	if (!strncmp(start, "fe", 2)) {
		start += 2;
		/* A blank isolates the vvideo2=() params */
		if (*start == ' ')
			goto suite;
		/* A comma introduces the msg_count */
		if (*start == ',') {
			++start;
		} else {
			vvideo_syntax(start);
			return NULL;
		}
	}
	/* There is something after "fe," which is not a comma */
	/* No end of string because we had vvideo2=() parameters */
	if (*start != ',') {
		char *end;

		msg_count = simple_strtoul(start, &end, 0);
		if (end == start) {
			vvideo_syntax(end);
			return NULL;
		}
		start = end;
		VVIDEO_LOG("original msg_count %ld\n", msg_count);
		if (msg_count <= 0)
			msg_count =
				vl->configs.count * VVIDEO2_DEV_AVERAGE_REQS;
		++msg_count; /* for abort message */
	}
	/* No end of string because we had vvideo2=() parameters */
	if (*start != ' ') {
		vvideo_syntax(start);
		return NULL;
	}
suite:
	/*
	 * Round up to next power of 2, as VMQ requires.
	 * nkops.nk_mask2bit() scans from LSB, so it is not suitable here.
	 */
	msg_count = vvideo_next_pow2(msg_count);
	VVIDEO_LOG("final msg_count %ld\n", msg_count);
	vl->xx_config.msg_count = msg_count;
	vl->xx_config.msg_max = sizeof(struct VVideo2Request);
	return &vl->xx_config;
}

static inline _Bool vvideo_config_async(const struct vvideo_config_t *config)
{
	if (config->async_mode == VVIDEO_ASYNC_MODE_DEFAULT)
		/* Enable for Scaler (50+) and disable for MFC (6-11) */
		return config->orig_node >= 50;
	return config->async_mode != VVIDEO_ASYNC_MODE_NO;
}

/*----- Helpers -----*/

#if defined VVIDEO_DEBUG || defined CONFIG_PROC_FS
static const char *vvideo_req_name(int req)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (req == x)                                                  \
			return &#x[12];                                        \
	} while (0)

	X(VVIDEO2_REQ_NONE);
	X(VVIDEO2_REQ_OPEN);
	X(VVIDEO2_REQ_RELEASE);
	X(VVIDEO2_REQ_IOCTL);
	X(VVIDEO2_REQ_ABORT);
	X(VVIDEO2_REQ_POLL);
	X(VVIDEO2_REQ_EVENT);

#undef X

	return "?";
}

static const char *vvideo_req_split(const int req_num, const unsigned int cmd)
{
	if (req_num == VVIDEO2_REQ_IOCTL) {
		const char *req_name = vvideo_ioctl_name(cmd);

		if (!strncmp(req_name, "VIDIOC_", 7))
			return req_name + 7;
		return req_name;
	}
	return vvideo_req_name(req_num);
}

static const char *vvideo_req_full(const struct VVideo2Request *req)
{
	return vvideo_req_split(req->req_num, req->u.ioctl.cmd);
}

static _Bool vvideo_non_zero(const void *buf0, size_t size)
{
	const char *buf = buf0;

	while (size--)
		if (*buf++)
			return true;
	return false;
}
#endif

/* Reserve a full hex digit/number for minor */

static unsigned int vvideo_round_hex(const unsigned int width)
{
	if (width < 4)
		return 4;
	if (width < 8)
		return 8;
	if (width < 12)
		return 12;
	return width;
}

static _Bool vvideo_cid_user_shared_handle(const unsigned int cid)
{
	return
		cid == V4L2_CID_MPEG_MFC_AV1_FILM_GRAIN_USER_SHARED_HANDLE ||
		cid == V4L2_CID_MPEG_MFC_HDR_USER_SHARED_HANDLE ||
		cid == V4L2_CID_MPEG_MFC_SET_USER_SHARED_HANDLE ||
		cid == V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_CH ||
		cid == V4L2_CID_MPEG_VIDEO_HEVC_HIERARCHICAL_CODING_LAYER_CH ||
		cid == V4L2_CID_MPEG_VIDEO_ROI_CONTROL ||
		cid == V4L2_CID_MPEG_VIDEO_VP8_HIERARCHICAL_CODING_LAYER_CH ||
		cid == V4L2_CID_MPEG_VIDEO_VP9_HIERARCHICAL_CODING_LAYER_CH;
}

/*----- Protocol binary compatibility check -----*/

VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_IOCTL_ARG_SIZE == 128);
VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_IOCTL_EXT_SIZE == 2560);
VVIDEO_STATIC_ASSERT(VVIDEO2_RES_PENDING == 0x12345678);
VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_NONE == 0);
VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_OPEN == 1);
VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_RELEASE == 2);
VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_IOCTL == 3);
VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_ABORT == 4);
VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_POLL == 5);
VVIDEO_STATIC_ASSERT(VVIDEO2_REQ_EVENT == 6);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Open, flags) == 0);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Ioctl, cmd) == 0);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Ioctl, arg_size) == 4);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Ioctl, ext_size) == 8);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Ioctl, arg_value) == 16);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Ioctl, arg) == 24);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Ioctl, ext) == 24+128);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Abort, transaction) == 0);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Poll, events) == 0);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Poll, cookie) == 8);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Poll, does_not_wait) == 12);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Poll, has_list) == 16);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Request, req_num) == 0);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Request, flags) == 2);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Request, result) == 4);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Request, be_context) == 8);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Request, transaction) == 12);
#ifndef CONFIG_VLX_VVIDEO2_DISABLE_VERSIONING
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Request, vvideo_ver) == 16);
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Request, u) == 32);
VVIDEO_STATIC_ASSERT(sizeof(struct VVideo2Request) == 32+4*4+8+128+2560);
#else
VVIDEO_STATIC_ASSERT(offsetof(struct VVideo2Request, u) == 16);
VVIDEO_STATIC_ASSERT(sizeof(struct VVideo2Request) == 16+4*4+8+128+2560);
#endif

VVIDEO_STATIC_ASSERT(sizeof(struct mfc_stored_dpb_info) == 12);
VVIDEO_STATIC_ASSERT(sizeof(struct mfc_dec_dpb_ref_info) == 4+12*32);
