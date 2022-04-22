/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmc
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_MMC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MMC_H
#include <trace/hooks/vendor_hooks.h>
struct mmc_queue;
struct mmc_host;

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_HOOK(android_vh_mmc_check_status,
	TP_PROTO(struct mmc_queue *mq, struct mmc_host *host, int issue_type, int *err),
	TP_ARGS(mq, host, issue_type, err));

#endif /* _TRACE_HOOK_MMC_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
