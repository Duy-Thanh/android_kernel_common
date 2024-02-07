/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_USB_ACCESSORY_H
#define __LINUX_USB_ACCESSORY_H

#include <linux/usb/composite.h>
#include <linux/usb/ch9.h>

#ifdef CONFIG_USB_CONFIGFS_F_ACC

bool acc_req_match_composite(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl);

int acc_setup_composite(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl);

void acc_disconnect(void);

#else

static inline bool acc_req_match_composite(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl)
{
	return false;
}

static inline int acc_setup_composite(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl)
{
	return 0;
}

static inline void acc_disconnect(void)
{
}
#endif /* CONFIG_USB_CONFIGFS_F_ACC */
#endif /* __LINUX_USB_ACCESSORY_H */
