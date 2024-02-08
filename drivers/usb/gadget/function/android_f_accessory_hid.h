/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2011-2024 Google LLC
 */
#ifndef __F_ACCESSORY_HID_H
#define __F_ACCESSORY_HID_H
#include <linux/usb/composite.h>
#include <linux/usb/ch9.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * The accessory hid functionality was originally tightly integrated in the
 * f_accessory driver. It appears to be mostly separate functionality that
 * could be handled by a standalone function driver, so begin the process of
 * breaking it out to simplify maintenence of both this and the f_accessory
 * driver.
 *
 * However, we do not want to completely separate the driver at this time as it
 * will require userspace changes. Therefore, provide an api similar to that
 * of the function api to be consumed by f_accessory.
 */
struct acc_hid_opts {
	spinlock_t lock;
	/* worker for registering and unregistering hid devices */
	struct work_struct work;

	/* list of active HID devices */
	struct list_head active_dev_list;

	/* list of new HID devices to register */
	struct list_head new_dev_list;

	/* list of dead HID devices to unregister */
	struct list_head dead_dev_list;
};

int acc_hid_init(struct acc_hid_opts *opts);
bool acc_hid_req_match(const struct usb_ctrlrequest *ctrl);
int acc_hid_setup(struct acc_hid_opts *opts, struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl);
int acc_hid_bind(struct acc_hid_opts *opts);
void acc_hid_unbind(struct acc_hid_opts *opts);
void acc_hid_disconnect(struct acc_hid_opts *opts);
void acc_hid_cleanup(struct acc_hid_opts *opts);
#endif /* __F_ACCESSORY_HID_H */
