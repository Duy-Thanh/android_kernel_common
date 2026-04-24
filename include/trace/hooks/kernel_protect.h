/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM kernel_protect

#if !defined(_TRACE_HOOK_KERNEL_PROTECT_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_KERNEL_PROTECT_H
#include <kconfig.h>
#include <trace/hooks/vendor_hooks.h>

/**
 * struct kp_el1_layout collects final
 * page aligned segment layour.
 * (offsets already KASLR-adjusted)
 */
struct kp_segment_info;

struct kp_layout_info;

/**
 * Vendor-side freeze: EL1 finidshed patching: EL2 may lock S2.
 * @layout - readonly, prepared by arch code.
 */
DECLARE_RESTRICTED_HOOK(android_rvh_freeze_kernel_protect,
			 TP_PROTO(const struct kp_layout_info *layout),
			 TP_ARGS(layout),
			 TP_CONDITION(IS_ENABLED(CONFIG_QCOM_KERNEL_PROTECT)));
#endif /* _TRACE_HOOK_PROTECTED_KERNEL_H */

#include <trace/define_trace.h>

