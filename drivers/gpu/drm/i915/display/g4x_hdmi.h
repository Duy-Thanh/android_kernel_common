/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020 Intel Corporation
 */

#ifndef _G4X_HDMI_H_
#define _G4X_HDMI_H_

#include <linux/types.h>

#include "i915_reg_defs.h"

enum port;
struct drm_atomic_state;
struct drm_connector;
struct drm_i915_private;

#ifdef I915
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
bool g4x_hdmi_init(struct drm_i915_private *dev_priv,
		   i915_reg_t hdmi_reg, enum port port);
int g4x_hdmi_connector_atomic_check(struct drm_connector *connector,
				    struct drm_atomic_state *state);
#else
static inline bool g4x_hdmi_init(struct drm_i915_private *dev_priv,
				 i915_reg_t hdmi_reg, int port)
{
	return false;
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
void g4x_hdmi_init(struct drm_i915_private *dev_priv,
		   i915_reg_t hdmi_reg, enum port port);
int g4x_hdmi_connector_atomic_check(struct drm_connector *connector,
				    struct drm_atomic_state *state);
#else
static inline void g4x_hdmi_init(struct drm_i915_private *dev_priv,
				 i915_reg_t hdmi_reg, int port)
{
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
}
static inline int g4x_hdmi_connector_atomic_check(struct drm_connector *connector,
						  struct drm_atomic_state *state)
{
	return 0;
}
#endif

#endif
