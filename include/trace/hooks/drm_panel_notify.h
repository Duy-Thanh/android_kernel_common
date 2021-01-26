/* SPDX-License-Identifier: GPL-2.0-only */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM drm_panel_notify

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DRM_PANEL_NOTIFY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DRM_PANEL_NOTIFY_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct drm_panel;

DECLARE_HOOK(android_vh_drm_panel_notify,
	TP_PROTO(struct drm_panel *panel, u32 mode, u32 event, u64 event_data),
	TP_ARGS(panel, mode, event, event_data))
#else

#define trace_android_vh_drm_panel_notify(panel, mode, event, event_data)
#endif

#endif /* _TRACE_HOOK_DRM_PANEL_NOTIFY_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
