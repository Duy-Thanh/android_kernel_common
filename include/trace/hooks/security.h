/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM security
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_SECURITY_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_SECURITY_H
#include <trace/hooks/vendor_hooks.h>

DECLARE_HOOK(android_vh_security_audit_log_cfi,
	TP_PROTO(unsigned long addr, unsigned long* target),
	TP_ARGS(addr, target));
DECLARE_RESTRICTED_HOOK(android_rvh_security_audit_log_usercopy,
	TP_PROTO(bool to_user, const char* name, unsigned long len),
	TP_ARGS(to_user, name, len), 1);
DECLARE_RESTRICTED_HOOK(android_rvh_security_audit_log_module_sign,
	TP_PROTO(int err),
	TP_ARGS(err), 1);

#endif

#include <trace/define_trace.h>