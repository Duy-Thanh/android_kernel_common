/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM io_pgtable_arm

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_IO_PGTABLE_ARM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_IO_PGTABLE_ARM_H

#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_arm_lpae_prot_to_pte,
		TP_PROTO(void *data, int prot, u64 *pte),
		TP_ARGS(data, prot, pte));

#endif /* _TRACE_HOOK_IO_PGTABLE_ARM_H */
/* This part must be outside protection */
#include <trace/define_trace.h>

