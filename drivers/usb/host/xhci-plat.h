/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xhci-plat.h - xHCI host controller driver platform Bus Glue.
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 */

#ifndef _XHCI_PLAT_H
#define _XHCI_PLAT_H

#include "xhci.h"

struct device;
struct platform_device;
struct usb_hcd;

struct xhci_plat_priv {
	const char *firmware_name;
	unsigned long long quirks;
	unsigned sideband_at_suspend:1;
	void (*plat_start)(struct usb_hcd *);
	int (*init_quirk)(struct usb_hcd *);
	int (*suspend_quirk)(struct usb_hcd *);
	int (*resume_quirk)(struct usb_hcd *);
};

/*
 * vendor_xhci_plat_quirks - vendor-specific xhci platform quirks.
 *
 * Passed from vendor drivers (e.g. dwc3-msm-core) through
 * vendor_xhci_plat_priv to xhci-plat.c, which stores the hook in
 * vendor_xhci_hcd (at the start of xhci->priv[]).
 */
struct vendor_xhci_plat_quirks {
	int (*handle_events)(struct xhci_hcd *xhci, struct xhci_interrupter *ir,
			     bool skip_events);
};

/*
 * vendor_xhci_plat_priv - vendor extension of xhci_plat_priv.
 *
 * struct xhci_plat_priv base MUST be the first member so that a pointer to
 * vendor_xhci_plat_priv can be safely cast to xhci_plat_priv * for the
 * standard copy in xhci_plat_probe().
 *
 * Passed via platform_device_add_data() from dwc3/host.c when vendor quirks
 * are present.
 */
struct vendor_xhci_plat_priv {
	struct xhci_plat_priv base;	/* MUST be first */
	const struct vendor_xhci_plat_quirks *vendor_quirks;
};

/*
 * vendor_xhci_hcd is now the outermost wrapper (hcd_priv → vendor_xhci_hcd
 * → xhci_hcd → priv[]).  xhci_plat_priv sits directly at xhci->priv[0],
 * so no offset arithmetic is needed.
 */
#define hcd_to_xhci_priv(h)	((struct xhci_plat_priv *)(hcd_to_xhci(h)->priv))
#define xhci_to_priv(x)		((struct xhci_plat_priv *)((x)->priv))

int xhci_plat_probe(struct platform_device *pdev, struct device *sysdev,
		    const struct xhci_plat_priv *priv_match);

void xhci_plat_remove(struct platform_device *dev);
extern const struct dev_pm_ops xhci_plat_pm_ops;

#endif	/* _XHCI_PLAT_H */
