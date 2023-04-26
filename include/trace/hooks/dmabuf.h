/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dmabuf

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DMA_BUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DMA_BUF_H

struct dma_buf;

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_ignore_dmabuf_vmap_bounds,
	     TP_PROTO(struct dma_buf *dma_buf, bool *ignore_bounds),
	     TP_ARGS(dma_buf, ignore_bounds));
DECLARE_HOOK(android_vh_dma_buf_sync_partial,
	     TP_PROTO(struct dma_buf *dma_buf, const void __user *buf, int *ret),
	     TP_ARGS(dma_buf, buf, ret));

#endif /* _TRACE_HOOK_DMA_BUF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
