/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM hyp_panic
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_HYP_PANIC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_HYP_PANIC_H
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_hyp_panic,
	TP_PROTO(u64 hyp_offset, u64 spsr, u64 elr_virt, u64 esr,
		u64 far, u64 hpfar, u64 par, uintptr_t vcpu, u64 elr_phys),
	TP_ARGS(hyp_offset, spsr, elr_virt, esr,
		far, hpfar, par, vcpu, elr_phys));

#endif /* _TRACE_HOOK_HYP_PANIC_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
