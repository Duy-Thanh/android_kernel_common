/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM fuse
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_FUSE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_FUSE_H
#include <trace/hooks/vendor_hooks.h>
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

struct wait_queue_head;
struct fuse_req;
DECLARE_HOOK(android_vh_queue_request_and_unlock,
	TP_PROTO(struct wait_queue_head *wq_head, bool sync),
	TP_ARGS(wq_head, sync));
DECLARE_HOOK(android_vh_fuse_request_send_ext,
	TP_PROTO(struct fuse_req *req, struct wait_queue_head *wq_head),
	TP_ARGS(req, wq_head));
DECLARE_HOOK(android_vh_fuse_request_end,
	TP_PROTO(struct task_struct *self),
	TP_ARGS(self));
<<<<<<< HEAD   (b90b7c3ecfee844d85f2d1cc4d737cbe1f3c3958 ANDROID: usb: gadget: f_accessory: prevent OOB write via HID)
||||||| BASE   (587520e1d7938c73d7d1ac5e9ed84f4a55e373b9 ANDROID: vendor_hooks: Add vendor hooks for fuse)
DECLARE_HOOK(android_vh_fuse_request_fetch,
	TP_PROTO(struct fuse_req *req, struct task_struct *self),
	TP_ARGS(req, self));
=======
DECLARE_HOOK(android_vh_fuse_request_end_ext,
	TP_PROTO(struct fuse_req *req, struct task_struct *self),
	TP_ARGS(req, self));
DECLARE_HOOK(android_vh_fuse_request_fetch,
	TP_PROTO(struct fuse_req *req, struct task_struct *self),
	TP_ARGS(req, self));
>>>>>>> CHANGE (21a5ca80dd14a8ba8e3bc1e2365afab7e717f98b ANDROID: vendor_hooks: Add hooks for fuse)

#endif /* _TRACE_HOOK_FUSE_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
