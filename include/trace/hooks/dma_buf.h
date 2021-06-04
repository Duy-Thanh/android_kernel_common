/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM dma_buf

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DMA_BUF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DMA_BUF_H

#include <linux/types.h>

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)

DECLARE_HOOK(android_vh_dma_buf_fd,
	TP_PROTO(struct dma_buf *dmabuf, int flags, int fd),
	TP_ARGS(dmabuf, flags, fd));
#else

#define trace_android_vh_dma_buf_fd(dmabuf, flags, fd)

#endif

#endif /* _TRACE_HOOK_DMA_BUF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
