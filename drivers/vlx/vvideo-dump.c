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

/*
 * This file is intended to be shared with user-mode test code, so it
 * is supposed to be #included in a proper context rather than being
 * self-sufficient for compilation.
 */

#ifndef V4L2_CAP_FENCES
#define V4L2_CAP_FENCES 0x20000000 /* Supports explicit synchronization */
#endif

const char *vvideo_ioctl_name_ex(unsigned long req, _Bool nr_only)
{
#define X(x)                                                                   \
	do {                                                                   \
		if ((!nr_only && req == x) || (nr_only && req == _IOC_NR(x)))  \
			return #x;                                             \
	} while (0)

	X(VIDIOC_QUERYCAP);
#ifdef VIDIOC_RESERVED
	X(VIDIOC_RESERVED);
#endif
	X(VIDIOC_ENUM_FMT);
	X(VIDIOC_G_FMT);
	X(VIDIOC_S_FMT);
	X(VIDIOC_REQBUFS);
	X(VIDIOC_QUERYBUF);
	X(VIDIOC_G_FBUF);
	X(VIDIOC_S_FBUF);
	X(VIDIOC_OVERLAY);
	X(VIDIOC_QBUF);
#ifdef VIDIOC_EXPBUF
	X(VIDIOC_EXPBUF);
#endif
	X(VIDIOC_DQBUF);
	X(VIDIOC_STREAMON);
	X(VIDIOC_STREAMOFF);
	X(VIDIOC_G_PARM);
	X(VIDIOC_S_PARM);
	X(VIDIOC_G_STD);
	X(VIDIOC_S_STD);
	X(VIDIOC_ENUMSTD);
	X(VIDIOC_ENUMINPUT);
	X(VIDIOC_G_CTRL);
	X(VIDIOC_S_CTRL);
	X(VIDIOC_G_TUNER);
	X(VIDIOC_S_TUNER);
	X(VIDIOC_G_AUDIO);
	X(VIDIOC_S_AUDIO);
	X(VIDIOC_QUERYCTRL);
	X(VIDIOC_QUERYMENU);
	X(VIDIOC_G_INPUT);
	X(VIDIOC_S_INPUT);
#ifdef VIDIOC_G_EDID
	X(VIDIOC_G_EDID);
#endif
#ifdef VIDIOC_S_EDID
	X(VIDIOC_S_EDID);
#endif
	X(VIDIOC_G_OUTPUT);
	X(VIDIOC_S_OUTPUT);
	X(VIDIOC_ENUMOUTPUT);
	X(VIDIOC_G_AUDOUT);
	X(VIDIOC_S_AUDOUT);
	X(VIDIOC_G_MODULATOR);
	X(VIDIOC_S_MODULATOR);
	X(VIDIOC_G_FREQUENCY);
	X(VIDIOC_S_FREQUENCY);
	X(VIDIOC_CROPCAP);
	X(VIDIOC_G_CROP);
	X(VIDIOC_S_CROP);
	X(VIDIOC_G_JPEGCOMP);
	X(VIDIOC_S_JPEGCOMP);
	X(VIDIOC_QUERYSTD);
	X(VIDIOC_TRY_FMT);
	X(VIDIOC_ENUMAUDIO);
	X(VIDIOC_ENUMAUDOUT);
	X(VIDIOC_G_PRIORITY);
	X(VIDIOC_S_PRIORITY);
	X(VIDIOC_G_SLICED_VBI_CAP);
	X(VIDIOC_LOG_STATUS);
	X(VIDIOC_G_EXT_CTRLS);
	X(VIDIOC_S_EXT_CTRLS);
	X(VIDIOC_TRY_EXT_CTRLS);
	X(VIDIOC_ENUM_FRAMESIZES);
	X(VIDIOC_ENUM_FRAMEINTERVALS);
	X(VIDIOC_G_ENC_INDEX);
	X(VIDIOC_ENCODER_CMD);
	X(VIDIOC_TRY_ENCODER_CMD);
	X(VIDIOC_DBG_S_REGISTER);
	X(VIDIOC_DBG_G_REGISTER);
#ifdef VIDIOC_S_HW_FREQ_SEEK
	X(VIDIOC_S_HW_FREQ_SEEK);
#endif
#ifdef VIDIOC_S_DV_TIMINGS
	X(VIDIOC_S_DV_TIMINGS);
#endif
#ifdef VIDIOC_G_DV_TIMINGS
	X(VIDIOC_G_DV_TIMINGS);
#endif
#ifdef VIDIOC_DQEVENT
	X(VIDIOC_DQEVENT);
#endif
#ifdef VIDIOC_SUBSCRIBE_EVENT
	X(VIDIOC_SUBSCRIBE_EVENT);
#endif
#ifdef VIDIOC_UNSUBSCRIBE_EVENT
	X(VIDIOC_UNSUBSCRIBE_EVENT);
#endif
#ifdef VIDIOC_CREATE_BUFS
	X(VIDIOC_CREATE_BUFS);
#endif
#ifdef VIDIOC_PREPARE_BUF
	X(VIDIOC_PREPARE_BUF);
#endif
#ifdef VIDIOC_G_SELECTION
	X(VIDIOC_G_SELECTION);
#endif
#ifdef VIDIOC_S_SELECTION
	X(VIDIOC_S_SELECTION);
#endif
#ifdef VIDIOC_DECODER_CMD
	X(VIDIOC_DECODER_CMD);
#endif
#ifdef VIDIOC_TRY_DECODER_CMD
	X(VIDIOC_TRY_DECODER_CMD);
#endif
#ifdef VIDIOC_ENUM_DV_TIMINGS
	X(VIDIOC_ENUM_DV_TIMINGS);
#endif
#ifdef VIDIOC_QUERY_DV_TIMINGS
	X(VIDIOC_QUERY_DV_TIMINGS);
#endif
#ifdef VIDIOC_DV_TIMINGS_CAP
	X(VIDIOC_DV_TIMINGS_CAP);
#endif
#ifdef VIDIOC_ENUM_FREQ_BANDS
	X(VIDIOC_ENUM_FREQ_BANDS);
#endif
#ifdef VIDIOC_DBG_G_CHIP_INFO
	X(VIDIOC_DBG_G_CHIP_INFO);
#endif
#ifdef VIDIOC_QUERY_EXT_CTRL
	X(VIDIOC_QUERY_EXT_CTRL);
#endif
#ifdef ION_IOC_ALLOC
	X(ION_IOC_ALLOC);
#endif
#ifdef ION_IOC_HEAP_QUERY
	X(ION_IOC_HEAP_QUERY);
#endif
	return "VIDIOC_?";

#undef X
}

const char *vvideo_ioctl_name(unsigned long req)
{
	return vvideo_ioctl_name_ex(req, 0);
}

void vvideo_fourcc_cookie(vvideo_printk_t pr, void *cookie, const char *legend,
			  __u32 f)
{
	pr(cookie, " %s:  %#x (%c%c%c%c)\n", legend, f, ((char *)&f)[0],
	   ((char *)&f)[1], ((char *)&f)[2], ((char *)&f)[3]);
}

void vvideo_pixelformat_cookie(vvideo_printk_t pr, void *c, __u32 f)
{
	vvideo_fourcc_cookie(pr, c, "pixelformat", f);
}

void vvideo_capabilities_cookie(vvideo_printk_t pr, void *cookie, __u32 c)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (c & x) {                                                   \
			pr(cookie, " %s", &#x[9]);                             \
			c &= ~x;                                               \
		}                                                              \
	} while (0)

	if (!c)
		return;
	pr(cookie, "   ");
	X(V4L2_CAP_VIDEO_CAPTURE);
	X(V4L2_CAP_VIDEO_OUTPUT);
	X(V4L2_CAP_VIDEO_OVERLAY);
	X(V4L2_CAP_VBI_CAPTURE);
	X(V4L2_CAP_VBI_OUTPUT);
	X(V4L2_CAP_SLICED_VBI_CAPTURE);
	X(V4L2_CAP_SLICED_VBI_OUTPUT);
	X(V4L2_CAP_RDS_CAPTURE);
	X(V4L2_CAP_VIDEO_OUTPUT_OVERLAY);
#ifdef V4L2_CAP_HW_FREQ_SEEK
	X(V4L2_CAP_HW_FREQ_SEEK);
#endif
#ifdef V4L2_CAP_RDS_OUTPUT
	X(V4L2_CAP_RDS_OUTPUT);
#endif
#ifdef V4L2_CAP_VIDEO_CAPTURE_MPLANE
	X(V4L2_CAP_VIDEO_CAPTURE_MPLANE);
#endif
#ifdef V4L2_CAP_VIDEO_OUTPUT_MPLANE
	X(V4L2_CAP_VIDEO_OUTPUT_MPLANE);
#endif
#ifdef V4L2_CAP_VIDEO_M2M_MPLANE
	X(V4L2_CAP_VIDEO_M2M_MPLANE);
#endif
#ifdef V4L2_CAP_VIDEO_M2M
	X(V4L2_CAP_VIDEO_M2M);
#endif
	X(V4L2_CAP_TUNER);
	X(V4L2_CAP_AUDIO);
	X(V4L2_CAP_RADIO);
#ifdef V4L2_CAP_MODULATOR
	X(V4L2_CAP_MODULATOR);
#endif
#ifdef V4L2_CAP_SDR_CAPTURE
	X(V4L2_CAP_SDR_CAPTURE);
#endif
#ifdef V4L2_CAP_EXT_PIX_FORMAT
	X(V4L2_CAP_EXT_PIX_FORMAT);
#endif
#ifdef V4L2_CAP_SDR_OUTPUT
	X(V4L2_CAP_SDR_OUTPUT);
#endif
#ifdef V4L2_CAP_META_CAPTURE
	X(V4L2_CAP_META_CAPTURE);
#endif
	if (c & 0x00800000) {
		pr(cookie, " %s", "META_CAPTURE");
		c &= ~0x00800000;
	}
	X(V4L2_CAP_READWRITE);
	X(V4L2_CAP_ASYNCIO);
	X(V4L2_CAP_STREAMING); /* Can use mmap() */
#ifdef V4L2_CAP_TOUCH
	X(V4L2_CAP_TOUCH);
#endif
#ifdef V4L2_CAP_DEVICE_CAPS
	X(V4L2_CAP_DEVICE_CAPS);
#endif
	X(V4L2_CAP_FENCES);
	if (c)
		pr(cookie, " MYSTERY_%#x", c);
	pr(cookie, "\n");

#undef X
}

void vvideo_v4l2_capability_cookie(vvideo_printk_t pr, void *cookie,
				   const struct v4l2_capability *c)
{
	pr(cookie, " driver:       %s\n", c->driver);
	pr(cookie, " card:         %s\n", c->card);
	pr(cookie, " bus_info:     %s\n", c->bus_info);
	pr(cookie, " version:      %#x (%d.%d.%d)\n", c->version,
	   (c->version >> 16) & 0xFF, (c->version >> 8) & 0xFF,
	   (c->version) & 0xFF);
	pr(cookie, " capabilities: %#x\n", c->capabilities);
	vvideo_capabilities_cookie(pr, cookie, c->capabilities);
#ifdef V4L2_CAP_DEVICE_CAPS
	pr(cookie, " device_caps:  %#x\n", c->device_caps);
	vvideo_capabilities_cookie(pr, cookie, c->device_caps);
#endif
	if (c->reserved[0] || c->reserved[1] || c->reserved[2]) {
		pr(cookie, " reserved:     %#x %#x %#x\n", c->reserved[0],
		   c->reserved[1], c->reserved[2]);
	}
}

const char *vvideo_v4l2_colorspace(const enum v4l2_colorspace c)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (c == x)                                                    \
			return #x;                                             \
	} while (0)

#ifdef V4L2_MAP_COLORSPACE_DEFAULT
	X(V4L2_COLORSPACE_DEFAULT);
#endif
	X(V4L2_COLORSPACE_SMPTE170M);
	X(V4L2_COLORSPACE_SMPTE240M);
	X(V4L2_COLORSPACE_REC709);
	X(V4L2_COLORSPACE_BT878);
	X(V4L2_COLORSPACE_470_SYSTEM_M);
	X(V4L2_COLORSPACE_470_SYSTEM_BG);
	X(V4L2_COLORSPACE_JPEG);
	X(V4L2_COLORSPACE_SRGB);
#ifdef V4L2_MAP_COLORSPACE_DEFAULT
#ifdef V4L2_BUF_CAP_SUPPORTS_MMAP
	X(V4L2_COLORSPACE_OPRGB);
#else
	/* enum const renamed between v4.19 and v4.20 */
	X(V4L2_COLORSPACE_ADOBERGB);
#endif
	X(V4L2_COLORSPACE_BT2020);
	X(V4L2_COLORSPACE_RAW);
	X(V4L2_COLORSPACE_DCI_P3);
#endif
	return "?";

#undef X
}

const char *vvideo_v4l2_field(const enum v4l2_field f)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (f == x)                                                    \
			return #x;                                             \
	} while (0)

	X(V4L2_FIELD_ANY);
	X(V4L2_FIELD_NONE);
	X(V4L2_FIELD_TOP);
	X(V4L2_FIELD_BOTTOM);
	X(V4L2_FIELD_INTERLACED);
	X(V4L2_FIELD_SEQ_TB);
	X(V4L2_FIELD_SEQ_BT);
	X(V4L2_FIELD_ALTERNATE);
	X(V4L2_FIELD_INTERLACED_TB);
	X(V4L2_FIELD_INTERLACED_BT);
	return "?";

#undef X
}

void vvideo_v4l2_pix_format_cookie(vvideo_printk_t pr, void *cookie,
				   const struct v4l2_pix_format *f)
{
	if (f->width)
		pr(cookie, " width:        %d\n", f->width);
	if (f->height)
		pr(cookie, " height:       %d\n", f->height);
	vvideo_pixelformat_cookie(pr, cookie, f->pixelformat);
	if (f->field)
		pr(cookie, " field:        %d (%s)\n", f->field,
		   vvideo_v4l2_field(f->field));
	pr(cookie, " bytesperline: %d\n", f->bytesperline);
	pr(cookie, " sizeimage:    %d\n", f->sizeimage);
	pr(cookie, " colorspace:   %d (%s)\n", f->colorspace,
	   vvideo_v4l2_colorspace(f->colorspace));
	pr(cookie, " priv:         %#x\n", f->priv);
#ifdef VVIDEO_UNUSED
	pr(cookie, " flags:        %d\n", f->flags);
	pr(cookie, " ycbcr_enc:    %d\n", f->ycbcr_enc);
	pr(cookie, " quantization: %d\n", f->quantization);
	pr(cookie, " xfer_func:    %d\n", f->xfer_func);
#endif
}

void vvideo_v4l2_window_cookie(vvideo_printk_t pr, void *cookie,
			       const struct v4l2_window *w)
{
#define X(x) pr(cookie, " " #x ": %d\n", w->x)

	if (w->w.left)
		X(w.left);
	if (w->w.top)
		X(w.top);
	X(w.width);
	X(w.height);
	pr(cookie, " field: %d (%s)\n", w->field, vvideo_v4l2_field(w->field));
	X(chromakey);
	pr(cookie, " clips: %p\n", (void *)w->clips);
	if (w->clips) {
		X(clips->c.left);
		X(clips->c.top);
		X(clips->c.width);
		X(clips->c.height);
	}
	X(clipcount);
	pr(cookie, " bitmap: %p\n", w->bitmap);
	X(global_alpha);

#undef X
}

void vvideo_v4l2_rect_cookie(vvideo_printk_t pr, void *cookie,
			     const struct v4l2_rect *r)
{
#define X(x) pr(cookie, " " #x ": %d\n", r->x)

	X(left);
	X(top);
	X(width);
	X(height);

#undef X
}

const char *vvideo_v4l2_buf_type(const enum v4l2_buf_type t)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (t == x)                                                    \
			return #x;                                             \
	} while (0)

	X(V4L2_BUF_TYPE_VIDEO_CAPTURE);
	X(V4L2_BUF_TYPE_VIDEO_OUTPUT);
	X(V4L2_BUF_TYPE_VIDEO_OVERLAY);
	X(V4L2_BUF_TYPE_VBI_CAPTURE);
	X(V4L2_BUF_TYPE_VBI_OUTPUT);
	X(V4L2_BUF_TYPE_SLICED_VBI_CAPTURE);
	X(V4L2_BUF_TYPE_SLICED_VBI_OUTPUT);
	X(V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY);
#ifdef V4L2_TYPE_IS_MULTIPLANAR
	X(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	X(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
#endif
#ifdef V4L2_CAP_SDR_CAPTURE
	X(V4L2_BUF_TYPE_SDR_CAPTURE);
#endif
#ifdef V4L2_CAP_SDR_OUTPUT
	X(V4L2_BUF_TYPE_SDR_OUTPUT);
#endif
#ifdef V4L2_CAP_META_CAPTURE
	X(V4L2_BUF_TYPE_META_CAPTURE);
#endif
	X(V4L2_BUF_TYPE_PRIVATE);
	return "V4L2_BUF_TYPE_?";

#undef X
}

const char *vvideo_v4l2_buf_type_short(const enum v4l2_buf_type t)
{
	return vvideo_v4l2_buf_type(t) + 14;
}

void vvideo_v4l2_buffer_flags(char *buf, size_t buf_len, __u32 f)
{
	int len = 0;

#define APPEND(format, args...)                                                \
	do {                                                                   \
		int ret = snprintf(buf + len, buf_len - len, format, ##args);  \
		if ((unsigned int) ret < buf_len - len)                        \
			len += ret;                                            \
	} while (0)

#define X(x)                                                                   \
	do {                                                                   \
		if (f & x) {                                                   \
			APPEND(" %s", &#x[14]);                                \
			f &= ~x;                                               \
		}                                                              \
	} while (0)

	X(V4L2_BUF_FLAG_MAPPED);
	X(V4L2_BUF_FLAG_QUEUED);
	X(V4L2_BUF_FLAG_DONE);
	X(V4L2_BUF_FLAG_KEYFRAME);
	X(V4L2_BUF_FLAG_PFRAME);
	X(V4L2_BUF_FLAG_BFRAME);
#ifdef V4L2_BUF_FLAG_ERROR
	X(V4L2_BUF_FLAG_ERROR);
#endif
	X(V4L2_BUF_FLAG_TIMECODE);
#ifdef V4L2_BUF_FLAG_PREPARED
	X(V4L2_BUF_FLAG_PREPARED);
#endif
#ifdef V4L2_BUF_FLAG_NO_CACHE_INVALIDATE
	X(V4L2_BUF_FLAG_NO_CACHE_INVALIDATE);
#endif
#ifdef V4L2_BUF_FLAG_NO_CACHE_CLEAN
	X(V4L2_BUF_FLAG_NO_CACHE_CLEAN);
#endif

#ifdef V4L2_BUF_FLAG_TIMESTAMP_MASK
	switch (f & V4L2_BUF_FLAG_TIMESTAMP_MASK) {
	case V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN:
		APPEND(" %s", &"V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN"[14]);
		break;

	case V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC:
		APPEND(" %s", &"V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC"[14]);
		break;

	case V4L2_BUF_FLAG_TIMESTAMP_COPY:
		APPEND(" %s", &"V4L2_BUF_FLAG_TIMESTAMP_COPY"[14]);
		break;

	default:
		APPEND(" ?%#x?", f & V4L2_BUF_FLAG_TIMESTAMP_MASK);
		break;
	}
	f &= ~V4L2_BUF_FLAG_TIMESTAMP_MASK;
#endif

#ifdef V4L2_BUF_FLAG_TSTAMP_SRC_MASK
	switch (f & V4L2_BUF_FLAG_TSTAMP_SRC_MASK) {
	case V4L2_BUF_FLAG_TSTAMP_SRC_EOF:
		APPEND(" %s", &"V4L2_BUF_FLAG_TSTAMP_SRC_EOF"[14]);
		break;

	case V4L2_BUF_FLAG_TSTAMP_SRC_SOE:
		APPEND(" %s", &"V4L2_BUF_FLAG_TSTAMP_SRC_SOE"[14]);
		break;

	default:
		APPEND(" ?%#x?", f & V4L2_BUF_FLAG_TSTAMP_SRC_MASK);
		break;
	}
	f &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
#endif

#ifdef V4L2_BUF_FLAG_LAST
	X(V4L2_BUF_FLAG_LAST);
#endif
	if (f)
		APPEND(" MYSTERY_%#x", f);

#undef X
#undef APPEND
}

const char *vvideo_v4l2_memory(const enum v4l2_memory m)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (m == x)                                                    \
			return #x;                                             \
	} while (0)

	X(V4L2_MEMORY_MMAP);
	X(V4L2_MEMORY_USERPTR);
	X(V4L2_MEMORY_OVERLAY);
#ifdef V4L2_CID_COLORFX_CBCR
	X(V4L2_MEMORY_DMABUF);
#endif
	return "?";

#undef X
}

#ifdef VIDEO_MAX_PLANES
void vvideo_v4l2_plane_pix_format_cookie(vvideo_printk_t pr, void *cookie,
					 const struct v4l2_plane_pix_format *f)
{
#define X(x) pr(cookie, " " #x ": %d\n", f->x)
	X(sizeimage);
	X(bytesperline);
	if (f->reserved[0])
		X(reserved[0]);
#undef X
}
#endif

#ifdef V4L2_TYPE_IS_MULTIPLANAR
void vvideo_v4l2_pix_format_mplane_cookie(vvideo_printk_t pr, void *cookie,
					const struct v4l2_pix_format_mplane *m)
{
	int plane;
	int ok;

#define X(x) pr(cookie, " " #x ": %d\n", m->x)

	if (m->width)
		X(width);
	if (m->height)
		X(height);
	vvideo_pixelformat_cookie(pr, cookie, m->pixelformat);
	if (m->field) {
		pr(cookie, " field: %d (%s)\n", m->field,
		   vvideo_v4l2_field(m->field));
	}
	if (m->colorspace) {
		pr(cookie, " colorspace: %d (%s)\n", m->colorspace,
		   vvideo_v4l2_colorspace(m->colorspace));
	}
#ifdef VIDEO_MAX_PLANES
	ok = 0;
	for (plane = 0; plane < VIDEO_MAX_PLANES; ++plane) {
		const struct v4l2_plane_pix_format *f = m->plane_fmt + plane;

		if (!f->sizeimage && !f->bytesperline && !f->reserved[0])
			continue;
		pr(cookie, " plane_fmt[%d]\n", plane);
		vvideo_v4l2_plane_pix_format_cookie(pr, cookie, f);
		ok = 1;
	}
	if (!ok)
		pr(cookie, " All %d plane_fmt[x] are zero\n", VIDEO_MAX_PLANES);
#endif
	X(num_planes);
#ifdef VVIDEO_UNUSED
	X(flags);
	X(ycbcr_enc);
	X(quantization);
	X(xfer_func);
#endif

#undef X
}
#endif

void vvideo_v4l2_vbi_format_cookie(vvideo_printk_t pr, void *cookie,
				   const struct v4l2_vbi_format *v)
{
#define X(x) pr(cookie, " " #x ": %d\n", v->x)

	X(sampling_rate);
	X(offset);
	X(samples_per_line);
	vvideo_fourcc_cookie(pr, cookie, "sample_format", v->sample_format);
	X(start[0]);
	X(start[1]);
	X(count[0]);
	X(count[1]);
	X(flags);
	X(reserved[0]);

#undef X
}

void vvideo_v4l2_sliced_vbi_format_cookie(vvideo_printk_t pr, void *cookie,
					const struct v4l2_sliced_vbi_format *s)
{
#define X(x) pr(cookie, " " #x ": %d\n", s->x)

	X(service_set);
	X(service_lines[0][0]);
	X(service_lines[1][0]);
	X(io_size);
	X(reserved[0]);
	X(reserved[1]);

#undef X
}

#ifdef V4L2_CAP_SDR_CAPTURE
void vvideo_v4l2_sdr_format_cookie(vvideo_printk_t pr, void *cookie,
				   const struct v4l2_sdr_format *s)
{
#define X(x) pr(cookie, " " #x ": %d\n", s->x)

	vvideo_pixelformat_cookie(pr, cookie, s->pixelformat);
	X(buffersize);
	X(reserved[0]);

#undef X
}
#endif

#ifdef V4L2_CAP_META_CAPTURE
void vvideo_v4l2_meta_format_cookie(vvideo_printk_t pr, void *cookie,
				    const struct v4l2_meta_format *m)
{
#define X(x) pr(cookie, " " #x ": %d\n", m->x)

	vvideo_pixelformat_cookie(pr, cookie, m->dataformat);
	X(buffersize);

#undef X
}
#endif

void vvideo_v4l2_format_cookie(vvideo_printk_t pr, void *cookie,
			       const struct v4l2_format *f)
{
	pr(cookie, " type: %d (%s)\n", f->type, vvideo_v4l2_buf_type(f->type));
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		vvideo_v4l2_pix_format_cookie(pr, cookie, &f->fmt.pix);
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
		vvideo_v4l2_pix_format_cookie(pr, cookie, &f->fmt.pix);
		break;
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
		vvideo_v4l2_window_cookie(pr, cookie, &f->fmt.win);
		break;
	case V4L2_BUF_TYPE_VBI_CAPTURE:
		vvideo_v4l2_vbi_format_cookie(pr, cookie, &f->fmt.vbi);
		break;
	case V4L2_BUF_TYPE_VBI_OUTPUT:
		vvideo_v4l2_vbi_format_cookie(pr, cookie, &f->fmt.vbi);
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
		vvideo_v4l2_sliced_vbi_format_cookie(pr, cookie,
						     &f->fmt.sliced);
		break;
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		vvideo_v4l2_sliced_vbi_format_cookie(pr, cookie,
						     &f->fmt.sliced);
		break;

#ifdef V4L2_TYPE_IS_MULTIPLANAR
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		vvideo_v4l2_pix_format_mplane_cookie(pr, cookie,
						     &f->fmt.pix_mp);
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		vvideo_v4l2_pix_format_mplane_cookie(pr, cookie,
						     &f->fmt.pix_mp);
		break;
#endif

#ifdef V4L2_CAP_SDR_CAPTURE
	case V4L2_BUF_TYPE_SDR_CAPTURE:
		vvideo_v4l2_sdr_format_cookie(pr, cookie, &f->fmt.sdr);
		break;
#endif

#ifdef V4L2_CAP_SDR_OUTPUT
	case V4L2_BUF_TYPE_SDR_OUTPUT:
		vvideo_v4l2_sdr_format_cookie(pr, cookie, &f->fmt.sdr);
		break;
#endif

#ifdef V4L2_CAP_META_CAPTURE
	case V4L2_BUF_TYPE_META_CAPTURE:
		vvideo_v4l2_meta_format_cookie(pr, cookie, &f->fmt.meta);
		break;
#endif

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
	case V4L2_BUF_TYPE_PRIVATE:
	default:
		pr(cookie, "Type %d not handled\n", f->type);
		break;
	}
}

#ifdef V4L2_TYPE_IS_MULTIPLANAR
void vvideo_v4l2_plane_cookie(vvideo_printk_t pr, void *c, const __u32 memory,
			      const struct v4l2_plane *p)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (p->x)                                                      \
			pr(c, "  " #x ": %d\n", p->x);                         \
	} while (0)

#define XL(x)                                                                  \
	do {                                                                   \
		if (p->x)                                                      \
			pr(c, "  " #x ": %#lx\n", p->x);                       \
	} while (0)

	X(bytesused);
	X(length);

	switch (memory) {
	case V4L2_MEMORY_MMAP:
		X(m.mem_offset);
		break;

	case V4L2_MEMORY_USERPTR:
		XL(m.userptr);
		break;

	case V4L2_MEMORY_OVERLAY:
	default:
		break;

#ifdef V4L2_CID_COLORFX_CBCR
	case V4L2_MEMORY_DMABUF:
		if (p->m.fd < 0)
			pr(c, "  m.fd: %#lx\n", p->m.fd);
		else
			X(m.fd);
		break;
#endif
	}
	X(data_offset);
	X(reserved[0]);

#undef X
#undef XL
}
#endif

void vvideo_v4l2_buffer_cookie(vvideo_printk_t pr, void *c,
			       const struct v4l2_buffer *b)
{
	char flags_buf[256];

#define X(x)                                                                   \
	do {                                                                   \
		if (b->x)                                                      \
			pr(c, " " #x ": %d\n", b->x);                          \
	} while (0)

#define XL(x)                                                                  \
	do {                                                                   \
		if (b->x)                                                      \
			pr(c, " " #x ": %#lx\n", b->x);                        \
	} while (0)

#define XP(x)                                                                  \
	do {                                                                   \
		if (b->x)                                                      \
			pr(c, " " #x ": %p\n", (void *)b->x);                  \
	} while (0)

	/* Always print b->index, even if 0 */
	pr(c, " index: %d\n", b->index);
	pr(c, " type: %d (%s)\n", b->type, vvideo_v4l2_buf_type(b->type));
	X(bytesused);
	vvideo_v4l2_buffer_flags(flags_buf, sizeof(flags_buf), b->flags);
	if (b->flags)
		pr(c, " flags: %#x (%s)\n", b->flags, flags_buf);
	if (b->field)
		pr(c, " field: %d (%s)\n", b->field,
		   vvideo_v4l2_field(b->field));
	if (b->timestamp.tv_sec || b->timestamp.tv_usec)
		pr(c, " timeval: %ld.%06ld s\n", b->timestamp.tv_sec,
		   b->timestamp.tv_usec);
#ifdef VVIDEO_UNUSED
	struct v4l2_timecode    timecode;
#endif
	X(sequence);
	if (b->memory)
		pr(c, " memory: %d (%s)\n", b->memory,
		   vvideo_v4l2_memory(b->memory));
#ifdef V4L2_TYPE_IS_MULTIPLANAR
	if (V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
		unsigned int plane;

		XP(m.planes);
		for (plane = 0; plane < b->length; ++plane) {
			pr(c, " Plane #%d:\n", plane);
			vvideo_v4l2_plane_cookie(pr, c, b->memory,
						 &b->m.planes[plane]);
		}
	} else
#endif
	{
		if (b->memory == V4L2_MEMORY_MMAP) {
			X(m.offset);
		} else if (b->memory == V4L2_MEMORY_USERPTR) {
			XL(m.userptr);
	#ifdef V4L2_CID_COLORFX_CBCR
		} else if (b->memory == V4L2_MEMORY_DMABUF) {
			if (b->m.fd < 0)
				pr(c, " m.fd: %#lx\n", b->m.fd);
			else
				X(m.fd);
	#endif
		} else {
			pr(c, " ???\n");
		}
	}
	X(length);
#ifdef V4L2_CID_COLORFX_CBCR
	if (b->reserved2)
		pr(c, " reserved2: %d (%#x)\n", b->reserved2, b->reserved2);
#endif
	X(reserved);

#undef XP
#undef XL
#undef X
}

const char *vvideo_v4l2_cid(const __u32 cid)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (cid == x)                                                  \
			return #x;                                             \
	} while (0)

	X(V4L2_CID_BRIGHTNESS);
	X(V4L2_CID_CONTRAST);
	X(V4L2_CID_SATURATION);
	X(V4L2_CID_HUE);
	X(V4L2_CID_AUDIO_VOLUME);
	X(V4L2_CID_AUDIO_BALANCE);
	X(V4L2_CID_AUDIO_BASS);
	X(V4L2_CID_AUDIO_TREBLE);
	X(V4L2_CID_AUDIO_MUTE);
	X(V4L2_CID_AUDIO_LOUDNESS);
	X(V4L2_CID_BLACK_LEVEL);
	X(V4L2_CID_AUTO_WHITE_BALANCE);
	X(V4L2_CID_DO_WHITE_BALANCE);
	X(V4L2_CID_RED_BALANCE);
	X(V4L2_CID_BLUE_BALANCE);
	X(V4L2_CID_GAMMA);
#ifdef VVIDEO_UNUSED
	X(V4L2_CID_WHITENESS);	/* Same value as V4L2_CID_GAMMA */
#endif
	X(V4L2_CID_EXPOSURE);
	X(V4L2_CID_AUTOGAIN);
	X(V4L2_CID_GAIN);
	X(V4L2_CID_HFLIP);
	X(V4L2_CID_VFLIP);
#ifdef V4L2_CID_POWER_LINE_FREQUENCY
	X(V4L2_CID_POWER_LINE_FREQUENCY);
#endif
#ifdef V4L2_CID_HUE_AUTO
	X(V4L2_CID_HUE_AUTO);
#endif
#ifdef V4L2_CID_WHITE_BALANCE_TEMPERATURE
	X(V4L2_CID_WHITE_BALANCE_TEMPERATURE);
#endif
#ifdef V4L2_CID_SHARPNESS
	X(V4L2_CID_SHARPNESS);
#endif
#ifdef V4L2_CID_BACKLIGHT_COMPENSATION
	X(V4L2_CID_BACKLIGHT_COMPENSATION);
#endif
#ifdef V4L2_CID_CHROMA_AGC
	X(V4L2_CID_CHROMA_AGC);
#endif
#ifdef V4L2_CID_COLOR_KILLER
	X(V4L2_CID_COLOR_KILLER);
#endif
#ifdef V4L2_CID_COLORFX
	X(V4L2_CID_COLORFX);
#endif
#ifdef V4L2_CID_AUTOBRIGHTNESS
	X(V4L2_CID_AUTOBRIGHTNESS);
#endif
#ifdef V4L2_CID_BAND_STOP_FILTER
	X(V4L2_CID_BAND_STOP_FILTER);
#endif
#ifdef V4L2_CID_ROTATE
	X(V4L2_CID_ROTATE);
#endif
#ifdef V4L2_CID_BG_COLOR
	X(V4L2_CID_BG_COLOR);
#endif
#ifdef V4L2_CID_CHROMA_GAIN
	X(V4L2_CID_CHROMA_GAIN);
#endif
#ifdef V4L2_CID_ILLUMINATORS_1
	X(V4L2_CID_ILLUMINATORS_1);
#endif
#ifdef V4L2_CID_ILLUMINATORS_2
	X(V4L2_CID_ILLUMINATORS_2);
#endif
#ifdef V4L2_CID_MIN_BUFFERS_FOR_CAPTURE
	X(V4L2_CID_MIN_BUFFERS_FOR_CAPTURE);
#endif
#ifdef V4L2_CID_MIN_BUFFERS_FOR_OUTPUT
	X(V4L2_CID_MIN_BUFFERS_FOR_OUTPUT);
#endif
#ifdef V4L2_CID_ALPHA_COMPONENT
	X(V4L2_CID_ALPHA_COMPONENT);
#endif
#ifdef V4L2_CID_COLORFX_CBCR
	X(V4L2_CID_COLORFX_CBCR);
#endif

	X(V4L2_CID_MPEG_STREAM_TYPE);
	X(V4L2_CID_MPEG_STREAM_PID_PMT);
	X(V4L2_CID_MPEG_STREAM_PID_AUDIO);
	X(V4L2_CID_MPEG_STREAM_PID_VIDEO);
	X(V4L2_CID_MPEG_STREAM_PID_PCR);
	X(V4L2_CID_MPEG_STREAM_PES_ID_AUDIO);
	X(V4L2_CID_MPEG_STREAM_PES_ID_VIDEO);
	X(V4L2_CID_MPEG_STREAM_VBI_FMT);
	X(V4L2_CID_MPEG_AUDIO_SAMPLING_FREQ);
	X(V4L2_CID_MPEG_AUDIO_ENCODING);
	X(V4L2_CID_MPEG_AUDIO_L1_BITRATE);
	X(V4L2_CID_MPEG_AUDIO_L2_BITRATE);
	X(V4L2_CID_MPEG_AUDIO_L3_BITRATE);
	X(V4L2_CID_MPEG_AUDIO_MODE);
	X(V4L2_CID_MPEG_AUDIO_MODE_EXTENSION);
	X(V4L2_CID_MPEG_AUDIO_EMPHASIS);
	X(V4L2_CID_MPEG_AUDIO_CRC);
	X(V4L2_CID_MPEG_AUDIO_MUTE);
#ifdef V4L2_CID_MPEG_AUDIO_AAC_BITRATE
	X(V4L2_CID_MPEG_AUDIO_AAC_BITRATE);
#endif
#ifdef V4L2_CID_MPEG_AUDIO_AC3_BITRATE
	X(V4L2_CID_MPEG_AUDIO_AC3_BITRATE);
#endif
#ifdef V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK
	X(V4L2_CID_MPEG_AUDIO_DEC_PLAYBACK);
#endif
#ifdef V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK
	X(V4L2_CID_MPEG_AUDIO_DEC_MULTILINGUAL_PLAYBACK);
#endif
	X(V4L2_CID_MPEG_VIDEO_ENCODING);
	X(V4L2_CID_MPEG_VIDEO_ASPECT);
	X(V4L2_CID_MPEG_VIDEO_B_FRAMES);
	X(V4L2_CID_MPEG_VIDEO_GOP_SIZE);
	X(V4L2_CID_MPEG_VIDEO_GOP_CLOSURE);
	X(V4L2_CID_MPEG_VIDEO_PULLDOWN);
	X(V4L2_CID_MPEG_VIDEO_BITRATE_MODE);
	X(V4L2_CID_MPEG_VIDEO_BITRATE);
	X(V4L2_CID_MPEG_VIDEO_BITRATE_PEAK);
	X(V4L2_CID_MPEG_VIDEO_TEMPORAL_DECIMATION);
	X(V4L2_CID_MPEG_VIDEO_MUTE);
	X(V4L2_CID_MPEG_VIDEO_MUTE_YUV);
#ifdef V4L2_CID_MPEG_VIDEO_DECODER_SLICE_INTERFACE
	X(V4L2_CID_MPEG_VIDEO_DECODER_SLICE_INTERFACE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER
	X(V4L2_CID_MPEG_VIDEO_DECODER_MPEG4_DEBLOCK_FILTER);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB
	X(V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE
	X(V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_HEADER_MODE
	X(V4L2_CID_MPEG_VIDEO_HEADER_MODE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MAX_REF_PIC
	X(V4L2_CID_MPEG_VIDEO_MAX_REF_PIC);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE
	X(V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES
	X(V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB
	X(V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE
	X(V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VBV_SIZE
	X(V4L2_CID_MPEG_VIDEO_VBV_SIZE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_DEC_PTS
	X(V4L2_CID_MPEG_VIDEO_DEC_PTS);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_DEC_FRAME
	X(V4L2_CID_MPEG_VIDEO_DEC_FRAME);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VBV_DELAY
	X(V4L2_CID_MPEG_VIDEO_VBV_DELAY);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER
	X(V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H263_I_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_H263_I_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H263_P_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_H263_P_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H263_B_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_H263_B_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H263_MIN_QP
	X(V4L2_CID_MPEG_VIDEO_H263_MIN_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H263_MAX_QP
	X(V4L2_CID_MPEG_VIDEO_H263_MAX_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_MIN_QP
	X(V4L2_CID_MPEG_VIDEO_H264_MIN_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_MAX_QP
	X(V4L2_CID_MPEG_VIDEO_H264_MAX_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM
	X(V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE
	X(V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE
	X(V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_I_PERIOD
	X(V4L2_CID_MPEG_VIDEO_H264_I_PERIOD);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_LEVEL
	X(V4L2_CID_MPEG_VIDEO_H264_LEVEL);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA
	X(V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA
	X(V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE
	X(V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_PROFILE
	X(V4L2_CID_MPEG_VIDEO_H264_PROFILE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT
	X(V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH
	X(V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE
	X(V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC
	X(V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING
	X(V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_SEI_FP_CURRENT_FRAME_0
	X(V4L2_CID_MPEG_VIDEO_H264_SEI_FP_CURRENT_FRAME_0);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE
	X(V4L2_CID_MPEG_VIDEO_H264_SEI_FP_ARRANGEMENT_TYPE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_FMO
	X(V4L2_CID_MPEG_VIDEO_H264_FMO);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE
	X(V4L2_CID_MPEG_VIDEO_H264_FMO_MAP_TYPE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_FMO_SLICE_GROUP
	X(V4L2_CID_MPEG_VIDEO_H264_FMO_SLICE_GROUP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_DIRECTION
	X(V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_DIRECTION);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_RATE
	X(V4L2_CID_MPEG_VIDEO_H264_FMO_CHANGE_RATE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_FMO_RUN_LENGTH
	X(V4L2_CID_MPEG_VIDEO_H264_FMO_RUN_LENGTH);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_ASO
	X(V4L2_CID_MPEG_VIDEO_H264_ASO);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_ASO_SLICE_ORDER
	X(V4L2_CID_MPEG_VIDEO_H264_ASO_SLICE_ORDER);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING
	X(V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE
	X(V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_TYPE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER
	X(V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_QP
	X(V4L2_CID_MPEG_VIDEO_H264_HIERARCHICAL_CODING_LAYER_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MPEG4_B_FRAME_QP
	X(V4L2_CID_MPEG_VIDEO_MPEG4_B_FRAME_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MPEG4_MIN_QP
	X(V4L2_CID_MPEG_VIDEO_MPEG4_MIN_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MPEG4_MAX_QP
	X(V4L2_CID_MPEG_VIDEO_MPEG4_MAX_QP);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL
	X(V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE
	X(V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_MPEG4_QPEL
	X(V4L2_CID_MPEG_VIDEO_MPEG4_QPEL);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS
	X(V4L2_CID_MPEG_VIDEO_VPX_NUM_PARTITIONS);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VPX_IMD_DISABLE_4X4
	X(V4L2_CID_MPEG_VIDEO_VPX_IMD_DISABLE_4X4);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES
	X(V4L2_CID_MPEG_VIDEO_VPX_NUM_REF_FRAMES);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VPX_FILTER_LEVEL
	X(V4L2_CID_MPEG_VIDEO_VPX_FILTER_LEVEL);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VPX_FILTER_SHARPNESS
	X(V4L2_CID_MPEG_VIDEO_VPX_FILTER_SHARPNESS);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_REF_PERIOD
	X(V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_REF_PERIOD);
#endif
#ifdef V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL
	X(V4L2_CID_MPEG_VIDEO_VPX_GOLDEN_FRAME_SEL);
#endif
	X(V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER_MODE);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_SPATIAL_FILTER);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_SPATIAL_FILTER_TYPE);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_SPATIAL_FILTER_TYPE);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER_MODE);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_TEMPORAL_FILTER);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_MEDIAN_FILTER_TYPE);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_BOTTOM);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_LUMA_MEDIAN_FILTER_TOP);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_BOTTOM);
	X(V4L2_CID_MPEG_CX2341X_VIDEO_CHROMA_MEDIAN_FILTER_TOP);
	X(V4L2_CID_MPEG_CX2341X_STREAM_INSERT_NAV_PACKETS);
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY
	X(V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY_ENABLE
	X(V4L2_CID_MPEG_MFC51_VIDEO_DECODER_H264_DISPLAY_DELAY_ENABLE);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE
	X(V4L2_CID_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE
	X(V4L2_CID_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_PADDING
	X(V4L2_CID_MPEG_MFC51_VIDEO_PADDING);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_PADDING_YUV
	X(V4L2_CID_MPEG_MFC51_VIDEO_PADDING_YUV);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_RC_FIXED_TARGET_BIT
	X(V4L2_CID_MPEG_MFC51_VIDEO_RC_FIXED_TARGET_BIT);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_RC_REACTION_COEFF
	X(V4L2_CID_MPEG_MFC51_VIDEO_RC_REACTION_COEFF);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_ACTIVITY
	X(V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_ACTIVITY);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_DARK
	X(V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_DARK);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_SMOOTH
	X(V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_SMOOTH);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_STATIC
	X(V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_STATIC);
#endif
#ifdef V4L2_CID_MPEG_MFC51_VIDEO_H264_NUM_REF_PIC_FOR_P
	X(V4L2_CID_MPEG_MFC51_VIDEO_H264_NUM_REF_PIC_FOR_P);
#endif

#ifndef VVIDEO_NO_EXYNOS
/* sadk9-sys-linux/drivers/media/platform/exynos/mfc/exynos_mfc_media.h */
	X(V4L2_CID_MPEG_MFC51_VIDEO_FRAME_TAG);
	X(V4L2_CID_MPEG_MFC51_VIDEO_CHECK_STATE);
	X(V4L2_CID_MPEG_MFC51_VIDEO_DISPLAY_STATUS);
	X(V4L2_CID_MPEG_MFC51_VIDEO_FRAME_POC);
	X(V4L2_CID_MPEG_VIDEO_SRC_BUF_FLAG);
	X(V4L2_CID_MPEG_VIDEO_DST_BUF_FLAG);
	X(V4L2_CID_MPEG_VIDEO_GDC_VOTF);
	X(V4L2_CID_MPEG_VIDEO_FRAME_ERROR_TYPE);
	X(V4L2_CID_MPEG_VIDEO_GOP_CTRL);
	X(V4L2_CID_MPEG_VIDEO_GET_DISPLAY_DELAY);
	X(V4L2_CID_MPEG_MFC51_VIDEO_FRAME_RATE);
	X(V4L2_CID_MPEG_VIDEO_QOS_RATIO);
	X(V4L2_CID_MPEG_MFC_GET_VERSION_INFO);
	X(V4L2_CID_MPEG_MFC_GET_EXTRA_BUFFER_SIZE);
	X(V4L2_CID_MPEG_MFC_SET_DYNAMIC_DPB_MODE);
	X(V4L2_CID_MPEG_MFC_SET_USER_SHARED_HANDLE);
	X(V4L2_CID_MPEG_MFC_GET_EXT_INFO);
	X(V4L2_CID_MPEG_MFC_GET_10BIT_INFO);
	X(V4L2_CID_MPEG_MFC_GET_DRIVER_INFO);
	X(V4L2_CID_MPEG_MFC_HDR_USER_SHARED_HANDLE);
	X(V4L2_CID_MPEG_VIDEO_HEVC_PREPEND_SPSPPS_TO_IDR);
	X(V4L2_CID_MPEG_VIDEO_SKIP_LAZY_UNMAP);
	X(V4L2_CID_MPEG_VIDEO_HIERARCHICAL_BITRATE_CTRL);
	X(V4L2_CID_MPEG_VIDEO_DECODING_ORDER);
	X(V4L2_CID_MPEG_MFC_AV1_FILM_GRAIN_USER_SHARED_HANDLE);
#ifdef SC_CID_FRAMERATE
	X(SC_CID_FRAMERATE);
#endif
	/* sadk9-sys-linux/include/uapi/linux/videodev2_exynos_media.h */
#ifdef V4L2_CID_CACHEABLE
	X(V4L2_CID_CACHEABLE);
#endif
#ifdef V4L2_CID_CSC_EQ_MODE
	X(V4L2_CID_CSC_EQ_MODE);
#endif
#ifdef V4L2_CID_CSC_EQ
	X(V4L2_CID_CSC_EQ);
#endif
#ifdef V4L2_CID_CSC_RANGE
	X(V4L2_CID_CSC_RANGE);
#endif
#ifdef V4L2_CID_CONTENT_PROTECTION
	X(V4L2_CID_CONTENT_PROTECTION);
#endif
#ifdef V4L2_SC_CID_SRC_CSPAN
	X(V4L2_SC_CID_SRC_CSPAN);
#endif
#ifdef V4L2_SC_CID_DST_CSPAN
	X(V4L2_SC_CID_DST_CSPAN);
#endif
#endif

	return "?";

#undef X
}

void vvideo_v4l2_control_cookie(vvideo_printk_t pr, void *cookie,
				const struct v4l2_control *c)
{
	const char *s = vvideo_v4l2_cid(c->id);

	if (*s != '?')
		pr(cookie, "%s: %d\n", s, c->value);
	else
		pr(cookie, "CID-%#x: %d\n", c->id, c->value);
}

void vvideo_v4l2_requestbuffers_cookie(vvideo_printk_t pr, void *cookie,
				       const struct v4l2_requestbuffers *r)
{
	pr(cookie, " count: %d\n", r->count);
	pr(cookie, " type: %d (%s)\n", r->type, vvideo_v4l2_buf_type(r->type));
	pr(cookie, " memory: %d (%s)\n", r->memory,
	   vvideo_v4l2_memory(r->memory));
	if (r->reserved[0] || r->reserved[1])
		pr(cookie, " reserved: %d %d\n", r->reserved[0],
		   r->reserved[1]);
}

void vvideo_v4l2_input_cookie(vvideo_printk_t pr, void *cookie,
			      const struct v4l2_input *i)
{
#define X(x) pr(cookie, " " #x ": %d\n", i->x)

	X(index);
	pr(cookie, " name: %s\n", i->name);
	pr(cookie, " type: %d (%s)\n", i->type,
	   i->type == V4L2_INPUT_TYPE_TUNER ?
		   "Tuner" :
		   i->type == V4L2_INPUT_TYPE_CAMERA ? "Camera" : "?");
	X(audioset);
	X(tuner);
	X(status);
#ifdef V4L2_TYPE_IS_MULTIPLANAR
	X(capabilities);
#endif
	X(reserved[0]);

#undef X
}

void vvideo_v4l2_output_cookie(vvideo_printk_t pr, void *cookie,
			       const struct v4l2_output *o)
{
#define X(x) pr(cookie, " " #x ": %d\n", o->x)

	X(index);
	pr(cookie, " name: %s\n", o->name);
	pr(cookie, " type: %d (%s)\n", o->type,
	   o->type == V4L2_OUTPUT_TYPE_MODULATOR ?
		   "Modulator" :
		   o->type == V4L2_OUTPUT_TYPE_ANALOG ?
		   "Analog" :
		   o->type == V4L2_OUTPUT_TYPE_ANALOGVGAOVERLAY ?
		   "AnalogVGAOverlay" :
		   "?");
	X(audioset);
	X(modulator);
	pr(cookie, " std: %ld\n", (long)o->std);
#ifdef V4L2_CAP_VIDEO_CAPTURE_MPLANE
	X(capabilities);
#endif
	X(reserved[0]);

#undef X
}

void vvideo_v4l2_queryctrl_cookie(vvideo_printk_t pr, void *cookie,
				  const struct v4l2_queryctrl *q)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (q->x)                                                      \
			pr(cookie, " " #x ": %d\n", q->x);                     \
	} while (0)

	pr(cookie, " id: %d (%#x)\n", q->id, q->id);
	X(type);
	pr(cookie, " name: %s\n", q->name);
	X(minimum);
	X(maximum);
	X(step);
	X(default_value);
	X(flags);
	X(reserved[0]);

#undef X
}

/* v4l2_fmtdesc only used with VIDIOC_ENUM_FMT */

void vvideo_v4l2_fmtdesc_cookie(vvideo_printk_t pr, void *cookie,
				const struct v4l2_fmtdesc *f)
{
#define X(x) pr(cookie, " " #x ": %d\n", f->x)

	X(index);
	pr(cookie, " type: %d (%s)\n", f->type, vvideo_v4l2_buf_type(f->type));
	if (f->flags) {
		pr(cookie, " flags: %d (%s %s)\n", f->flags,
		   f->flags & V4L2_FMT_FLAG_COMPRESSED ? "Compressed" : "",
#ifdef V4L2_FMT_FLAG_EMULATED
		   f->flags & V4L2_FMT_FLAG_EMULATED ? "Emulated" :
#endif
						       "");
	}
	pr(cookie, " description: %s\n", f->description);
	vvideo_pixelformat_cookie(pr, cookie, f->pixelformat);
	if (f->reserved[0])
		X(reserved[0]);

#undef X
}

void vvideo_v4l2_crop_cookie(vvideo_printk_t pr, void *cookie,
			     const struct v4l2_crop *c)
{
#define X(x) pr(cookie, " " #x ": %d\n", c->x)

	pr(cookie, " type: %d (%s)\n", c->type, vvideo_v4l2_buf_type(c->type));
	if (c->c.left)
		X(c.left);
	if (c->c.top)
		X(c.top);
	X(c.width);
	X(c.height);

#undef X
}

void vvideo_v4l2_cropcap_cookie(vvideo_printk_t pr, void *cookie,
				const struct v4l2_cropcap *cc)
{
#define X(x)                                                                   \
	do {                                                                   \
		if (cc->x)                                                     \
			pr(cookie, " " #x ": %d\n", cc->x);                    \
	} while (0)

	pr(cookie, " type: %d (%s)\n", cc->type,
	   vvideo_v4l2_buf_type(cc->type));
	X(bounds.left);
	X(bounds.top);
	X(bounds.width);
	X(bounds.height);
	X(defrect.left);
	X(defrect.top);
	X(defrect.width);
	X(defrect.height);
	pr(cookie, " pixelaspect: %d %d\n", cc->pixelaspect.numerator,
	   cc->pixelaspect.denominator);

#undef X
}

void vvideo_v4l2_streamparm_cookie(vvideo_printk_t pr, void *cookie,
				   const struct v4l2_streamparm *s)
{
#define X(x) pr(cookie, " " #x ": %d\n", s->x)

	pr(cookie, " type: %d (%s)\n", s->type, vvideo_v4l2_buf_type(s->type));
	pr(cookie, " parm.capture.capability: %#x (%s %s)\n",
	   s->parm.capture.capability,
	   s->parm.capture.capability & V4L2_MODE_HIGHQUALITY ?
		   "V4L2_MODE_HIGHQUALITY" :
		   "",
	   s->parm.capture.capability & V4L2_CAP_TIMEPERFRAME ?
		   "V4L2_CAP_TIMEPERFRAME" :
		   "");
	pr(cookie, " parm.capture.capturemode: %#x (%s %s)\n",
	   s->parm.capture.capturemode,
	   s->parm.capture.capturemode & V4L2_MODE_HIGHQUALITY ?
		   "V4L2_MODE_HIGHQUALITY" :
		   "",
	   s->parm.capture.capturemode & V4L2_CAP_TIMEPERFRAME ?
		   "V4L2_CAP_TIMEPERFRAME" :
		   "");
	/* struct v4l2_fract  timeperframe */
	X(parm.capture.extendedmode);
	X(parm.capture.readbuffers);
	X(parm.capture.reserved[0]);
	X(parm.output.outputmode);
	X(parm.output.writebuffers);

#undef X
}
