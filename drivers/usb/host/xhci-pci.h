/* SPDX-License-Identifier: GPL-2.0 */
<<<<<<< HEAD   (714dbb ANDROID: GKI: remove CONFIG_EDAC from arm64 gki_defconfig)
/* Copyright (C) 2008 Intel Corp. */

#ifndef XHCI_PCI_H
#define XHCI_PCI_H

int xhci_pci_setup(struct usb_hcd *hcd);

int xhci_pci_probe(struct pci_dev *pdev,
		   const struct pci_device_id *id);

void xhci_pci_remove(struct pci_dev *dev);

int xhci_pci_suspend(struct usb_hcd *hcd, bool do_wakeup);

int xhci_pci_resume(struct usb_hcd *hcd, bool hibernated);
=======
/* Copyright (C) 2019-2020 Linaro Limited */

#ifndef XHCI_PCI_H
#define XHCI_PCI_H

#if IS_ENABLED(CONFIG_USB_XHCI_PCI_RENESAS)
int renesas_xhci_check_request_fw(struct pci_dev *dev,
				  const struct pci_device_id *id);
void renesas_xhci_pci_exit(struct pci_dev *dev);

#else
static int renesas_xhci_check_request_fw(struct pci_dev *dev,
					 const struct pci_device_id *id)
{
	return 0;
}

static void renesas_xhci_pci_exit(struct pci_dev *dev) { };

#endif

struct xhci_driver_data {
	u64 quirks;
	const char *firmware;
};
>>>>>>> BRANCH (e611c0 Merge tag 'usb-5.8-rc1' of git://git.kernel.org/pub/scm/linu)

#endif
