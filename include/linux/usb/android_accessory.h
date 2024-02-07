/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ANDROID_ACCESSORY_H
#define __ANDROID_ACCESSORY_H

#include <linux/usb/composite.h>
#include <linux/usb/ch9.h>

#ifdef CONFIG_ANDROID_USB_CONFIGFS_F_ACC

bool android_acc_req_match_composite(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl);

int android_acc_setup_composite(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl);

void android_acc_disconnect(void);

#else

static inline bool android_acc_req_match_composite(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl)
{
	return false;
}

static inline int android_acc_setup_composite(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl)
{
	return 0;
}

static inline void android_acc_disconnect(void)
{
}
#endif /* CONFIG_ANDROID_USB_CONFIGFS_F_ACC */
#endif /* __ANDROID_ACCESSORY_H */
