/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM experiments

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_EXPERIMENTS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_EXPERIMENTS_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_uname_trace,
        TP_PROTO(unsigned int n),
        TP_ARGS(n));

#endif /* _TRACE_HOOK_EXPERIMENTS_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
