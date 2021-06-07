/*  SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb

#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H

#include <linux/tracepoint.h>
#include <trace/hooks/vendor_hooks.h>

struct usb_ctrlrequest;
struct ffs_function;

DECLARE_HOOK(android_vh_ffs_func_req_match,
        TP_PROTO(const struct ffs_function *func, const struct usb_ctrlrequest *creq, bool *allow, bool *ret),
		        TP_ARGS(func, creq, allow, ret));

/*  macro versions of hooks are no longer required */

#endif /*  _TRACE_HOOK_USB_H */
/*  This part must be outside protection */
#include <trace/define_trace.h>
