/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: CK Hu <ck.hu@mediatek.com>
 */

#ifndef _MTK_PLANE_H_
#define _MTK_PLANE_H_

#include <drm/drm_crtc.h>
#include <linux/types.h>

#define AFBC_DATA_BLOCK_WIDTH 32
#define AFBC_DATA_BLOCK_HEIGHT 8
#define AFBC_HEADER_BLOCK_SIZE 16
#define AFBC_HEADER_ALIGNMENT 1024

struct mtk_plane_pending_state {
	bool				config;
	bool				enable;
	dma_addr_t			addr;
	dma_addr_t			hdr_addr;
	unsigned int			pitch;
	unsigned int			hdr_pitch;
	unsigned int			format;
	unsigned long long		modifier;
	unsigned int			x;
	unsigned int			y;
	unsigned int			width;
	unsigned int			height;
	unsigned int			rotation;
	bool				dirty;
	bool				async_dirty;
	bool				async_config;
	enum drm_color_encoding		color_encoding;
};

struct mtk_plane_state {
	struct drm_plane_state		base;
	struct mtk_plane_pending_state	pending;
};

static inline struct mtk_plane_state *
to_mtk_plane_state(struct drm_plane_state *state)
{
	return container_of(state, struct mtk_plane_state, base);
}

int mtk_plane_init(struct drm_device *dev, struct drm_plane *plane,
		   unsigned long possible_crtcs, enum drm_plane_type type,
		   unsigned int supported_rotations, const u32 blend_modes,
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
		   const u32 *formats, size_t num_formats,
		   bool supports_afbc, unsigned int plane_idx);
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
		   const u32 *formats, size_t num_formats, unsigned int plane_idx);
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
#endif
