/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fault
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_FAULT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FAULT_H

#include <trace/hooks/vendor_hooks.h>

struct pt_regs;
DECLARE_RESTRICTED_HOOK(android_rvh_die_kernel_fault,
	TP_PROTO(const char *msg, unsigned long addr, unsigned long esr, struct pt_regs *regs),
	TP_ARGS(msg, addr, esr, regs), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_do_sea,
	TP_PROTO(unsigned long addr, unsigned long esr, struct pt_regs *regs),
	TP_ARGS(addr, esr, regs), 1);

DECLARE_RESTRICTED_HOOK(android_rvh_do_sp_pc_abort,
	TP_PROTO(unsigned long addr, unsigned long esr, struct pt_regs *regs),
	TP_ARGS(addr, esr, regs),
	TP_CONDITION(!user_mode(regs)));

DECLARE_HOOK(android_vh_try_fixup_sea,
	TP_PROTO(unsigned long addr, unsigned long esr, struct pt_regs *regs,
		 bool *can_fixup),
	TP_ARGS(addr, esr, regs, can_fixup));

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
DECLARE_HOOK(android_vh_kernel_nofault,
	TP_PROTO(bool *use_nofault),
	TP_ARGS(use_nofault));

||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
#endif /* _TRACE_HOOK_FAULT_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
