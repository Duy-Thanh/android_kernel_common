/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dmabuf

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DMABUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DMABUF_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_dma_heap_buffer_alloc_start,
		TP_PROTO(const char *name, size_t len,
			u32 fd_flags, u64 heap_flags),
		TP_ARGS(name, len, fd_flags, heap_flags));
DECLARE_HOOK(android_vh_dma_heap_buffer_alloc_end,
		TP_PROTO(const char *name, size_t len),
		TP_ARGS(name, len));
<<<<<<< HEAD   (db15f4990be1b8865ae651c9d748d43c91bb9d13 Merge 6.18.13 into android17-6.18)
||||||| BASE   (affdb774d7ec489843c01b1e82a1c4626af9e0c6 ANDROID: GKI: update symbol list file for xiaomi)
struct dma_buf;
DECLARE_HOOK(android_vh_dma_buf_attr_show_start,
		TP_PROTO(struct dma_buf **dmabuf),
		TP_ARGS(dmabuf));
DECLARE_HOOK(android_vh_dma_buf_attr_show_end,
		TP_PROTO(struct dma_buf *dmabuf),
		TP_ARGS(dmabuf));
=======
struct dma_buf;
DECLARE_HOOK(android_vh_dma_buf_attr_show_start,
		TP_PROTO(struct dma_buf **dmabuf),
		TP_ARGS(dmabuf));
DECLARE_HOOK(android_vh_dma_buf_attr_show_end,
		TP_PROTO(struct dma_buf *dmabuf),
		TP_ARGS(dmabuf));
DECLARE_HOOK(android_vh_dma_buf_release,
		TP_PROTO(struct dma_buf *data),
		TP_ARGS(data));
>>>>>>> CHANGE (1d3cdeb464ce95d74494c87ca07c3d1ac17cadf1 ANDROID: GKI: dma-buf: add vendor hook for dma_buf_release)
#endif /* _TRACE_HOOK_DMABUF_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
