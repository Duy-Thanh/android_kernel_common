/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM bl_hib

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_S2D_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_S2D_H

#include <trace/hooks/vendor_hooks.h>

struct block_device;
struct arch_hibernate_hdr;

DECLARE_HOOK(android_vh_disable_randomization,
       TP_PROTO(struct block_device *resume_block),
       TP_ARGS(resume_block));
DECLARE_HOOK(android_vh_check_randomization,
	TP_PROTO(struct block_device *block, bool *noswap_randomize),
	TP_ARGS(block, noswap_randomize));
DECLARE_HOOK(android_vh_init_arch_hibernate_hdr,
	TP_PROTO(struct arch_hibernate_hdr *hdr, u64 phys_addr),
	TP_ARGS(hdr, phys_addr));

#endif /* _TRACE_HOOK_S2D_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
