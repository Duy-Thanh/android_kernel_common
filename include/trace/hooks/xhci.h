/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM xhci
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks
#if !defined(_TRACE_HOOK_XHCI_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_XHCI_H
#include <trace/hooks/vendor_hooks.h>
#include "../drivers/usb/host/xhci.h"
/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */

DECLARE_HOOK(android_vh_xhci_offload_init,
	TP_PROTO(struct xhci_hcd *xhci, int *result),
	TP_ARGS(xhci, result));

DECLARE_RESTRICTED_HOOK(android_rvh_xhci_offload_cleanup,
	TP_PROTO(struct xhci_hcd *xhci, int *result),
	TP_ARGS(xhci, result), 1);

DECLARE_HOOK(android_vh_xhci_is_offload_enabled,
	TP_PROTO(struct xhci_hcd *xhci, struct xhci_virt_device *vdev,
		 unsigned int ep_index, bool *enabled),
	TP_ARGS(xhci, vdev, ep_index, enabled));

DECLARE_RESTRICTED_HOOK(android_rvh_xhci_alloc_dcbaa,
	TP_PROTO(struct xhci_hcd *xhci, gfp_t flags),
	TP_ARGS(xhci, flags), 1);

DECLARE_HOOK(android_vh_xhci_free_dcbaa,
	TP_PROTO(struct xhci_hcd *xhci, int *result),
	TP_ARGS(xhci, result));

DECLARE_RESTRICTED_HOOK(android_rvh_xhci_alloc_transfer_ring,
	TP_PROTO(struct xhci_hcd *xhci, u32 endpoint_type, enum xhci_ring_type ring_type,
		 unsigned int max_packet, gfp_t mem_flags, struct xhci_ring **ep_ring),
	TP_ARGS(xhci, endpoint_type, ring_type, max_packet, mem_flags, ep_ring), 1);

DECLARE_HOOK(android_vh_xhci_free_transfer_ring,
	TP_PROTO(struct xhci_hcd *xhci, struct xhci_virt_device *virt_dev, unsigned int ep_index),
	TP_ARGS(xhci, virt_dev, ep_index));

DECLARE_HOOK(android_vh_xhci_offload_skip_urb,
	TP_PROTO(struct xhci_hcd *xhci, struct urb *urb, bool *bypass),
	TP_ARGS(xhci, urb, bypass));

#endif /* _TRACE_HOOK_XHCI_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
