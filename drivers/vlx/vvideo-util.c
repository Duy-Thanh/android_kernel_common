// SPDX-License-Identifier: GPL-2.0+
/*
 ****************************************************************
 *
 *  Component: VLX virtual video driver
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

/*----- System includes -----*/

#include <linux/version.h>
#include <asm/ioctl.h>
#include <linux/videodev2.h>
#ifdef V4L2_CHIP_MATCH_SUBDEV
#include <linux/v4l2-subdev.h>
#endif
#include <linux/seq_file.h>
#include <nk/nkern.h>

/*----- Local includes -----*/

/* Do not include vvideo protocol file, as utils are independent */
#include "vvideo-util.h"

/*----- Local configuration -----*/

/*
 * In VVIDEO_DMABUF_HOLD mode, we must match gids only, because
 * frontend might allocate a new cred for the same buffer
 * and this would create duplicates in backend DMABUF table,
 * making it larger than max allowed file descriptors.
 */
#define VVIDEO_MATCH_ONLY_GID

/* Display credentials along with gids in debug traces */
// #define VVIDEO_VERBOSE

/*----- V4L2 trace code -----*/

#include "vvideo-dump.c"

/*----- seq_file calls -----*/

void vvideo_fourcc(struct seq_file *seq, __u32 f)
{
	seq_printf(seq, "%c%c%c%c", ((char *)&f)[0], ((char *)&f)[1],
		   ((char *)&f)[2], ((char *)&f)[3]);
}

void vvideo_capabilities(struct seq_file *seq, __u32 c)
{
	vvideo_capabilities_cookie((vvideo_printk_t)seq_printf, seq, c);
}

void vvideo_v4l2_capability(struct seq_file *seq,
			    const struct v4l2_capability *c)
{
	vvideo_v4l2_capability_cookie((vvideo_printk_t)seq_printf, seq, c);
}

void vvideo_v4l2_pix_format(struct seq_file *seq,
			    const struct v4l2_pix_format *f)
{
	vvideo_v4l2_pix_format_cookie((vvideo_printk_t)seq_printf, seq, f);
}

void vvideo_v4l2_window(struct seq_file *seq, const struct v4l2_window *w)
{
	vvideo_v4l2_window_cookie((vvideo_printk_t)seq_printf, seq, w);
}

void vvideo_v4l2_rect(struct seq_file *seq, const struct v4l2_rect *r)
{
	vvideo_v4l2_rect_cookie((vvideo_printk_t)seq_printf, seq, r);
}

#ifdef V4L2_TYPE_IS_MULTIPLANAR
void vvideo_v4l2_plane(struct seq_file *seq, const __u32 memory,
		       const struct v4l2_plane *p)
{
	vvideo_v4l2_plane_cookie((vvideo_printk_t)seq_printf, seq, memory, p);
}
#endif

void vvideo_v4l2_buffer(struct seq_file *seq, const struct v4l2_buffer *b)
{
	vvideo_v4l2_buffer_cookie((vvideo_printk_t)seq_printf, seq, b);
}

/*----- Driver helpers -----*/

void vvideo_printk(void *ctx, const char *fmt, ...)
{
	va_list args;

	(void)ctx;
	va_start(args, fmt);
	vprintk(fmt, args);
	va_end(args);
}

char vvideo_link_state(const int state)
{
	switch (state) {
	case NK_DEV_VLINK_ON:
		return 'O';
	case NK_DEV_VLINK_RESET:
		return 'R';
	case NK_DEV_VLINK_OFF:
		return 'F';
	default:
		break;
	}
	return '?';
}

static int vvideo_config_syntax(const char *c_info,
				const struct vvideo_config_t *c)
{
	pr_err(
"c_info '%s' parsed wrong, giving name '%s' orig_node %d final_node %d async_mode %d\n",
		c_info, c->name, c->orig_node, c->final_node, c->async_mode);
	return -EINVAL;
}

/* Returns 0 on succes, -ERROR otherwise */

int vvideo_config_parse(const char *const c_info, struct vvideo_config_t *c)
{
	const char *start = c_info;
	int consumed = 0;
	int ret;

	c->name[0] = '\0';
	c->orig_node = -1;
	c->final_node = -1;
	c->async_mode = VVIDEO_ASYNC_MODE_DEFAULT;

	/* "%n" does not count for the returned count of items */
	if (*start != ',') { /* Comma after <name> */
	/*
	 * Assignment to "ret" is only used to satisfy some code checkers
	 * for used return values; it could possibly be also typecast to
	 * "void" to mark it used, or replaced with an "if (sscanf(...)) {}".
	 * Indeed, if "name" field is non-empty
	 * at "start", sscanf() works and parses it. If "name" is empty,
	 * "consumed" remains at 0, and routine exits at "!*start" test
	 * below. This allows to achieve 100% branch and line coverage.
	 */
		ret = sscanf(start, "%63[^,]%n", c->name, &consumed);
		start += consumed;
	}
	if (!*start)
		return 0;
	if (*start != ',')
		return vvideo_config_syntax(c_info, c);
	++start;
	if (!*start)
		return 0;
	if (*start != ',') { /* Comma after <orig_node> */
		ret = sscanf(start, "%d%n", &c->orig_node, &consumed);
		if (ret != 1)
			return vvideo_config_syntax(c_info, c);
		start += consumed;
	}
	if (!*start || *start == ')')
		return 0;
	if (*start != ',')
		return vvideo_config_syntax(c_info, c);
	++start;
	if (!*start)
		return 0;
	if (*start != ',') {
		ret = sscanf(start, "%d%n", &c->final_node, &consumed);
		if (ret != 1)
			return vvideo_config_syntax(c_info, c);
		start += consumed;
	}
	if (!*start || *start == ')')
		return 0;
	if (*start != ',')
		return vvideo_config_syntax(c_info, c);
	++start;
	if (!*start)
		return 0;
	if (*start != ',') {
		int async_mode;

		ret = sscanf(start, "%d%n", &async_mode, &consumed);
		if (ret != 1)
			return vvideo_config_syntax(c_info, c);
		if (async_mode != VVIDEO_ASYNC_MODE_DEFAULT &&
			async_mode != VVIDEO_ASYNC_MODE_NO &&
			async_mode != VVIDEO_ASYNC_MODE_YES) {
			return vvideo_config_syntax(c_info, c);
		}
		c->async_mode = async_mode;
	}
	return 0;
}

const char *vvideo_dmabuf_gid_format(const struct vvideo_dmabuf_gid_t *gid,
				     char *buf, size_t bufsize)
{
#ifdef VVIDEO_LOCAL_GID
	snprintf(buf, bufsize, "%#lx", gid->dma_buf);
#else
#ifdef VVIDEO_VERBOSE
	snprintf(buf, bufsize,
"%x/%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		 gid->gid, gid->cred.b[0], gid->cred.b[1], gid->cred.b[2],
		 gid->cred.b[3], gid->cred.b[4], gid->cred.b[5], gid->cred.b[6],
		 gid->cred.b[7], gid->cred.b[8], gid->cred.b[9],
		 gid->cred.b[10], gid->cred.b[11], gid->cred.b[12],
		 gid->cred.b[13], gid->cred.b[14], gid->cred.b[15]);
#else
	snprintf(buf, bufsize, "%x", gid->gid);
#endif
#endif
	return buf;
}

_Bool vvideo_dmabuf_gid_same(const struct vvideo_dmabuf_gid_t *g1,
			     const struct vvideo_dmabuf_gid_t *g2)
{
#ifdef VVIDEO_LOCAL_GID
	return g1->dma_buf == g2->dma_buf;
#else
	return g1->gid == g2->gid
#ifndef VVIDEO_MATCH_ONLY_GID
	       && !memcmp(&g1->cred, &g2->cred, sizeof(g1->cred))
#endif
		;
#endif
}
