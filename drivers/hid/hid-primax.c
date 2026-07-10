// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID driver for primax and similar keyboards with in-band modifiers
 *
 * Copyright 2011 Google Inc. All Rights Reserved
 *
 * Author:
 *	Terry Lambert <tlambert@google.com>
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

static int px_raw_event(struct hid_device *hid, struct hid_report *report,
	 u8 *data, int size)
{
	int idx = size;

	switch (report->id) {
	case 0:		/* keyboard input */
		/*
		 * Convert in-band modifier key values into out of band
		 * modifier bits and pull the key strokes from the report.
		 * Thus a report data set which looked like:
		 *
		 * [00][00][E0][30][00][00][00][00]
		 * (no modifier bits + "Left Shift" key + "1" key)
		 *
		 * Would be converted to:
		 *
		 * [01][00][00][30][00][00][00][00]
		 * (Left Shift modifier bit + "1" key)
		 *
		 * As long as it's in the size range, the upper level
		 * drivers don't particularly care if there are in-band
		 * 0-valued keys, so they don't stop parsing.
		 */
		while (--idx > 1) {
			if (data[idx] < 0xE0 || data[idx] > 0xE7)
				continue;
			data[0] |= (1 << (data[idx] - 0xE0));
			data[idx] = 0;
		}
<<<<<<< HEAD   (06b7b3a8d324c3a708319548981dccfdb3d60263 Revert "batman-adv: tp_meter: directly shut down timer on cl)
		__hid_report_raw_event(hid, HID_INPUT_REPORT, data, size, size, 0);
||||||| BASE   (ad3d1628a46134276546d7a12fedf04be9979158 USB: serial: cypress_m8: fix memory corruption with small en)
		hid_report_raw_event(hid, HID_INPUT_REPORT, data, size, 0);
=======
		hid_report_raw_event(hid, HID_INPUT_REPORT, data, size, size, 0);
>>>>>>> BRANCH (8c15ac42db0c0afe723602ef483cf7109a4af0ac HID: core: Fix size_t specifier in hid_report_raw_event())
		return 1;

	default:	/* unknown report */
		/* Unknown report type; pass upstream */
		hid_info(hid, "unknown report type %d\n", report->id);
		break;
	}

	return 0;
}

static const struct hid_device_id px_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_PRIMAX, USB_DEVICE_ID_PRIMAX_KEYBOARD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, px_devices);

static struct hid_driver px_driver = {
	.name = "primax",
	.id_table = px_devices,
	.raw_event = px_raw_event,
};
module_hid_driver(px_driver);

MODULE_AUTHOR("Terry Lambert <tlambert@google.com>");
MODULE_LICENSE("GPL");
