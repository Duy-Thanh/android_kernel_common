/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM security
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SECURITY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SECURITY_H
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_security_audit_log,
	TP_PROTO(u32 type, int err, bool to_user, const char* name, unsigned long param1, unsigned long param2),
	TP_ARGS(type, err, to_user, name, param1, param2));

#endif

#include <trace/define_trace.h>
