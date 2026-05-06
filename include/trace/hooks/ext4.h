#undef TRACE_SYSTEM
#define TRACE_SYSTEM ext4

#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_EXT4_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_EXT4_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_HOOK(android_vh_disable_ext4_fsync,
  TP_PROTO(bool *disabled),
  TP_ARGS(disabled));

#endif /* _TRACE_HOOK_EXT4_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
