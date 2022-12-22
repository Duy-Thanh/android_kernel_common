/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM iova

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_IOVA_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IOVA_H
#include <trace/hooks/vendor_hooks.h>

struct iova_domain;
struct iova;
DECLARE_RESTRICTED_HOOK(android_rvh_iommu_alloc_insert_iova,
	TP_PROTO(struct iova_domain *iovad, unsigned long size,
		unsigned long limit_pfn, struct iova *new_iova,
		bool size_aligned, int *ret),
	TP_ARGS(iovad, size, limit_pfn, new_iova, size_aligned, ret),
	1);

#endif /* _TRACE_HOOK_IOVA_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
