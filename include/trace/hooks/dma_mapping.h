/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM dma_mapping

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_DMA_MAPPING_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_DMA_MAPPING_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_dma_alloc_attrs,
		TP_PROTO(struct device *dev, unsigned long *attrs),
		TP_ARGS(dev, attrs));

#endif /* _TRACE_HOOK_DMA_MAPPING_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

