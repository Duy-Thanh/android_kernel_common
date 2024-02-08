// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2011-2024 Google LLC
 */
#include <linux/hid.h>
#include <linux/hiddev.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/usb/composite.h>
#include <linux/usb/f_accessory.h>
#include "f_accessory_hid.h"

struct acc_hid_dev {
	struct list_head list;
	struct hid_device *hid;
	struct acc_hid_opts *opts;
	/* accessory defined ID */
	int id;
	/* HID report descriptor */
	u8 *report_desc;
	/* length of HID report descriptor */
	int report_desc_len;
	/* number of bytes of report_desc we have received so far */
	int report_desc_offset;
};

static int acc_hid_parse(struct hid_device *hid)
{
	struct acc_hid_dev *hdev = hid->driver_data;

	hid_parse_report(hid, hdev->report_desc, hdev->report_desc_len);
	return 0;
}

// Required by the hid_ll_driver, so do nothing.
static int acc_hid_start(struct hid_device *hid)
{
	return 0;
}

// Required by the hid_ll_driver, so do nothing.
static void acc_hid_stop(struct hid_device *hid)
{
}

// Required by the hid_ll_driver, so do nothing.
static int acc_hid_open(struct hid_device *hid)
{
	return 0;
}

// Required by the hid_ll_driver, so do nothing.
static void acc_hid_close(struct hid_device *hid)
{
}

static int acc_hid_raw_request(struct hid_device *hid, unsigned char reportnum,
	__u8 *buf, size_t len, unsigned char rtype, int reqtype)
{
	return 0;
}

static struct hid_ll_driver acc_hid_ll_driver = {
	.parse = acc_hid_parse,
	.start = acc_hid_start,
	.stop = acc_hid_stop,
	.open = acc_hid_open,
	.close = acc_hid_close,
	.raw_request = acc_hid_raw_request,
};

static struct acc_hid_dev *acc_hid_new(struct acc_hid_opts *opts,
		int id, int desc_len)
{
	struct acc_hid_dev *hdev;

	hdev = kzalloc(sizeof(*hdev), GFP_ATOMIC);
	if (!hdev)
		return NULL;
	hdev->report_desc = kzalloc(desc_len, GFP_ATOMIC);
	if (!hdev->report_desc) {
		kfree(hdev);
		return NULL;
	}
	hdev->opts = opts;
	hdev->id = id;
	hdev->report_desc_len = desc_len;

	return hdev;
}

static struct acc_hid_dev *acc_hid_get(struct list_head *list, int id)
{
	struct acc_hid_dev *hid;

	list_for_each_entry(hid, list, list) {
		if (hid->id == id)
			return hid;
	}
	return NULL;
}

static int acc_register_hid(struct acc_hid_opts *opts, int id, int desc_length)
{
	struct acc_hid_dev *hid;
	unsigned long flags;

	/* report descriptor length must be > 0 */
	if (desc_length <= 0)
		return -EINVAL;

	spin_lock_irqsave(&opts->lock, flags);
	/* replace HID if one already exists with this ID */
	hid = acc_hid_get(&opts->active_dev_list, id);
	if (!hid)
		hid = acc_hid_get(&opts->new_dev_list, id);
	if (hid)
		list_move(&hid->list, &opts->dead_dev_list);

	hid = acc_hid_new(opts, id, desc_length);
	if (!hid) {
		spin_unlock_irqrestore(&opts->lock, flags);
		return -ENOMEM;
	}

	list_add(&hid->list, &opts->new_dev_list);
	spin_unlock_irqrestore(&opts->lock, flags);

	/* schedule work to register the HID device */
	schedule_work(&opts->work);
	return 0;
}

static int acc_unregister_hid(struct acc_hid_opts *opts, int id)
{
	struct acc_hid_dev *hid;
	unsigned long flags;

	spin_lock_irqsave(&opts->lock, flags);
	hid = acc_hid_get(&opts->active_dev_list, id);
	if (!hid)
		hid = acc_hid_get(&opts->new_dev_list, id);
	if (!hid) {
		spin_unlock_irqrestore(&opts->lock, flags);
		return -EINVAL;
	}

	list_move(&hid->list, &opts->dead_dev_list);
	spin_unlock_irqrestore(&opts->lock, flags);

	schedule_work(&opts->work);
	return 0;
}
static int acc_hid_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret)
		return ret;
	return hid_hw_start(hdev, HID_CONNECT_DEFAULT);
}

static const struct hid_device_id acc_hid_table[] = {
	{ HID_USB_DEVICE(HID_ANY_ID, HID_ANY_ID) },
	{ }
};

static struct hid_driver acc_hid_driver = {
	.name = "USB accessory",
	.id_table = acc_hid_table,
	.probe = acc_hid_probe,
};

static void kill_all_hid_devices(struct acc_hid_opts *opts)
{
	struct acc_hid_dev *hid;
	struct list_head *entry, *temp;
	unsigned long flags;

	spin_lock_irqsave(&opts->lock, flags);
	list_for_each_safe(entry, temp, &opts->active_dev_list) {
		hid = list_entry(entry, struct acc_hid_dev, list);
		list_del(&hid->list);
		list_add(&hid->list, &opts->dead_dev_list);
	}
	list_for_each_safe(entry, temp, &opts->new_dev_list) {
		hid = list_entry(entry, struct acc_hid_dev, list);
		list_del(&hid->list);
		list_add(&hid->list, &opts->dead_dev_list);
	}
	spin_unlock_irqrestore(&opts->lock, flags);

	schedule_work(&opts->work);
}

static int acc_hid_dev_init(struct acc_hid_dev *hdev)
{
	struct hid_device *hid;
	int ret;

	hid = hid_allocate_device();
	if (IS_ERR(hid))
		return PTR_ERR(hid);

	hid->ll_driver = &acc_hid_ll_driver;
	//hid->dev.parent = acc_device.this_device;

	hid->bus = BUS_USB;
	hid->vendor = HID_ANY_ID;
	hid->product = HID_ANY_ID;
	hid->driver_data = hdev;
	ret = hid_add_device(hid);
	if (ret) {
		pr_err("can't add hid device: %d\n", ret);
		hid_destroy_device(hid);
		return ret;
	}

	hdev->hid = hid;
	return 0;
}

static void acc_hid_dev_delete(struct acc_hid_dev *hid)
{
	kfree(hid->report_desc);
	kfree(hid);
}

static void acc_hid_work(struct work_struct *work)
{
	struct acc_hid_opts *opts;
	struct list_head *entry, *temp;
	struct acc_hid_dev *hid;
	struct list_head new_list, dead_list;
	unsigned long flags;

	opts = container_of(work, struct acc_hid_opts, work);

	INIT_LIST_HEAD(&new_list);

	spin_lock_irqsave(&opts->lock, flags);

	/* copy hids that are ready for initialization to new_list */
	list_for_each_safe(entry, temp, &opts->new_dev_list) {
		hid = list_entry(entry, struct acc_hid_dev, list);
		if (hid->report_desc_offset == hid->report_desc_len)
			list_move(&hid->list, &new_list);
	}

	if (list_empty(&opts->dead_dev_list)) {
		INIT_LIST_HEAD(&dead_list);
	} else {
		/* move all of dev->dead_dev_list to dead_list */
		dead_list.prev = opts->dead_dev_list.prev;
		dead_list.next = opts->dead_dev_list.next;
		dead_list.next->prev = &dead_list;
		dead_list.prev->next = &dead_list;
		INIT_LIST_HEAD(&opts->dead_dev_list);
	}

	spin_unlock_irqrestore(&opts->lock, flags);

	/* register new HID devices */
	list_for_each_safe(entry, temp, &new_list) {
		hid = list_entry(entry, struct acc_hid_dev, list);
		if (acc_hid_dev_init(hid)) {
			pr_err("can't add HID device %p\n", hid);
			acc_hid_dev_delete(hid);
		} else {
			spin_lock_irqsave(&opts->lock, flags);
			list_move(&hid->list, &opts->active_dev_list);
			spin_unlock_irqrestore(&opts->lock, flags);
		}
	}

	/* remove dead HID devices */
	list_for_each_safe(entry, temp, &dead_list) {
		hid = list_entry(entry, struct acc_hid_dev, list);
		list_del(&hid->list);
		if (hid->hid)
			hid_destroy_device(hid->hid);
		acc_hid_dev_delete(hid);
	}
}

int acc_hid_bind(struct acc_hid_opts *opts)
{
	return hid_register_driver(&acc_hid_driver);
}
EXPORT_SYMBOL_GPL(acc_hid_bind);

void acc_hid_unbind(struct acc_hid_opts *opts)
{
	hid_unregister_driver(&acc_hid_driver);
	kill_all_hid_devices(opts);
}
EXPORT_SYMBOL_GPL(acc_hid_unbind);


bool acc_hid_req_match(const struct usb_ctrlrequest *ctrl)
{
	u8 bRequestType = ctrl->bRequestType;
	u8 bRequest = ctrl->bRequest;

	if (bRequestType == (USB_DIR_OUT | USB_TYPE_VENDOR)) {
		switch (bRequest) {
		case ACCESSORY_REGISTER_HID:
		case ACCESSORY_UNREGISTER_HID:
		case ACCESSORY_SET_HID_REPORT_DESC:
		case ACCESSORY_SEND_HID_EVENT:
			return true;
		default:
			return false;
		}
	}
	return false;
}
EXPORT_SYMBOL_GPL(acc_hid_req_match);

static void acc_hid_complete_setup_noop(struct usb_ep *ep,
		struct usb_request *req)
{
	/* Nothing to do here */
}

static void acc_complete_set_hid_report_desc(struct usb_ep *ep,
		struct usb_request *req)
{
	struct acc_hid_dev *hid = req->context;
	struct acc_hid_opts *opts = hid->opts;
	int length = req->actual;

	if (req->status != 0) {
		pr_err("%s, err %d\n", __func__, req->status);
		return;
	}

	memcpy(hid->report_desc + hid->report_desc_offset, req->buf, length);
	hid->report_desc_offset += length;
	if (hid->report_desc_offset == hid->report_desc_len) {
		/* After we have received the entire report descriptor
		 * we schedule work to initialize the HID device
		 */
		schedule_work(&opts->work);
	}
}

static void acc_complete_send_hid_event(struct usb_ep *ep,
		struct usb_request *req)
{
	struct acc_hid_dev *hid = req->context;
	int length = req->actual;

	if (req->status != 0) {
		pr_err("%s, err %d\n", __func__, req->status);
		return;
	}

	hid_report_raw_event(hid->hid, HID_INPUT_REPORT, req->buf, length, 1);
}

int acc_hid_setup(struct acc_hid_opts *opts, struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl)
{
	struct acc_hid_dev *hid;
	int value = -EOPNOTSUPP;
	int offset;
	u8 bRequestType = ctrl->bRequestType;
	u8 bRequest = ctrl->bRequest;
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wLength = le16_to_cpu(ctrl->wLength);
	unsigned long flags;

	if (bRequestType == (USB_DIR_OUT | USB_TYPE_VENDOR)) {
		if (bRequest == ACCESSORY_REGISTER_HID) {
			cdev->req->complete = acc_hid_complete_setup_noop;
			value = acc_register_hid(opts, wValue, wIndex);
		} else if (bRequest == ACCESSORY_UNREGISTER_HID) {
			cdev->req->complete = acc_hid_complete_setup_noop;
			value = acc_unregister_hid(opts, wValue);
		} else if (bRequest == ACCESSORY_SET_HID_REPORT_DESC) {
			spin_lock_irqsave(&opts->lock, flags);
			hid = acc_hid_get(&opts->new_dev_list, wValue);
			spin_unlock_irqrestore(&opts->lock, flags);
			if (!hid) {
				value = -EINVAL;
				goto err;
			}
			offset = wIndex;
			if (offset != hid->report_desc_offset
				|| offset + wLength > hid->report_desc_len) {
				value = -EINVAL;
				goto err;
			}
			cdev->req->context = hid;
			cdev->req->complete = acc_complete_set_hid_report_desc;
			value = wLength;
		} else if (bRequest == ACCESSORY_SEND_HID_EVENT) {
			spin_lock_irqsave(&opts->lock, flags);
			hid = acc_hid_get(&opts->active_dev_list, wValue);
			spin_unlock_irqrestore(&opts->lock, flags);
			if (!hid) {
				value = -EINVAL;
				goto err;
			}
			cdev->req->context = hid;
			cdev->req->complete = acc_complete_send_hid_event;
			value = wLength;
		}
	}

	if (value >= 0) {
		cdev->req->zero = 0;
		cdev->req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, cdev->req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "%s setup response queue error\n",
					__func__);
	}

err:
	if (value == -EOPNOTSUPP)
		VDBG(cdev,
			"unknown class-specific control req %02x.%02x v%04x i%04x l%u\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
	return value;
}
EXPORT_SYMBOL_GPL(acc_hid_setup);

void acc_hid_cleanup(struct acc_hid_opts *opts)
{
	cancel_work_sync(&opts->work);
}
EXPORT_SYMBOL_GPL(acc_hid_cleanup);

void acc_hid_disconnect(struct acc_hid_opts *opts)
{
	/* unregister all HID devices if USB is disconnected */
	kill_all_hid_devices(opts);
}
EXPORT_SYMBOL_GPL(acc_hid_disconnect);

int acc_hid_init(struct acc_hid_opts *opts)
{
	INIT_LIST_HEAD(&opts->active_dev_list);
	INIT_LIST_HEAD(&opts->new_dev_list);
	INIT_LIST_HEAD(&opts->dead_dev_list);
	INIT_WORK(&opts->work, acc_hid_work);
	return 0;
}
EXPORT_SYMBOL_GPL(acc_hid_init);
