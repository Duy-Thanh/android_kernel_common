/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM module

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_MODULE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_MODULE_H
#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
#if defined(CONFIG_TRACEPOINTS) && defined(CONFIG_ANDROID_VENDOR_HOOKS)
struct module;
DECLARE_HOOK(android_vh_set_module_permission, TP_PROTO(const struct module *mod, bool after_init, bool rodata_enabled), TP_ARGS(mod, after_init, rodata_enabled));
#else
#define trace_android_vh_set_module_permission(mod, after_init, rodata_enabled)
#endif

#endif /* _TRACE_HOOK_MODULE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
