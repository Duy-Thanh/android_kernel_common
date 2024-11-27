/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM pci
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_PCI_VH_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_PCI_VH_H
#include <trace/hooks/vendor_hooks.h>

struct pci_dev;
typedef int __bitwise pci_power_t;

DECLARE_HOOK(android_vh_platform_pci_power_manageable,
		TP_PROTO(struct pci_dev *dev, bool *manageable),
		TP_ARGS(dev, manageable));
DECLARE_HOOK(android_vh_platform_pci_set_power_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t t, int *ret),
		TP_ARGS(dev, t, ret));
DECLARE_HOOK(android_vh_platform_pci_get_power_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t *state),
		TP_ARGS(dev, state));
DECLARE_HOOK(android_vh_platform_pci_refresh_power_state,
		TP_PROTO(struct pci_dev *dev),
		TP_ARGS(dev));
DECLARE_HOOK(android_vh_platform_pci_choose_state,
		TP_PROTO(struct pci_dev *dev, pci_power_t *state),
		TP_ARGS(dev, state));
DECLARE_HOOK(android_vh_platform_pci_set_wakeup,
		TP_PROTO(struct pci_dev *dev, bool enable, int *ret),
		TP_ARGS(dev, enable, ret));
DECLARE_HOOK(android_vh_platform_pci_need_resume,
		TP_PROTO(struct pci_dev *dev, bool *need_resume),
		TP_ARGS(dev, need_resume));
DECLARE_HOOK(android_vh_platform_pci_bridge_d3,
		TP_PROTO(struct pci_dev *dev, bool *d3),
		TP_ARGS(dev, d3));

#endif /* _TRACE_HOOK_PCI_VH_H */
/* This part must be outside protection */
#include <trace/define_trace.h>
