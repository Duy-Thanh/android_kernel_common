// SPDX-License-Identifier: GPL-2.0
/*
 * Gadget Function Driver for Android USB accessories
 *
 * Copyright 2011-2024 Google LLC
 *
 */

/* #define DEBUG */
/* #define VERBOSE_DEBUG */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/kref.h>
#include <linux/kernel.h>

#include <linux/types.h>
#include <linux/file.h>
#include <linux/device.h>
#include <linux/miscdevice.h>

#include <linux/usb.h>
#include <linux/usb/ch9.h>
#include <linux/usb/f_accessory.h>

#include <linux/configfs.h>
#include <linux/usb/composite.h>

#include "u_accessory.h"
#include "f_accessory_hid.h"

#define MAX_INST_NAME_LEN 40
#define BULK_BUFFER_SIZE 16384

#define PROTOCOL_VERSION 2
#define MISC_DEVICE_NAME "usb_accessory"

/* String IDs */
#define INTERFACE_STRING_INDEX 0

static struct acc_misc_dev *misc_dev;

static struct usb_interface_descriptor acc_interface_desc = {
	.bLength                = USB_DT_INTERFACE_SIZE,
	.bDescriptorType        = USB_DT_INTERFACE,
	.bInterfaceNumber       = 0,
	.bNumEndpoints          = 2,
	.bInterfaceClass        = USB_CLASS_VENDOR_SPEC,
	.bInterfaceSubClass     = USB_SUBCLASS_VENDOR_SPEC,
	.bInterfaceProtocol     = 0,
};

static struct usb_endpoint_descriptor acc_superspeedplus_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor acc_superspeedplus_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor acc_superspeedplus_comp_desc = {
	.bLength                = sizeof(acc_superspeedplus_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =         0, */
	/* .bmAttributes =      0, */
};

static struct usb_endpoint_descriptor acc_superspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(1024),
};

static struct usb_endpoint_descriptor acc_superspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor acc_superspeed_comp_desc = {
	.bLength                = sizeof(acc_superspeed_comp_desc),
	.bDescriptorType        = USB_DT_SS_ENDPOINT_COMP,

	/* the following 2 values can be tweaked if necessary */
	/* .bMaxBurst =         0, */
	/* .bmAttributes =      0, */
};

static struct usb_endpoint_descriptor acc_highspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(512),
};

static struct usb_endpoint_descriptor acc_highspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize         = cpu_to_le16(512),
};

static struct usb_endpoint_descriptor acc_fullspeed_in_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_IN,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor acc_fullspeed_out_desc = {
	.bLength                = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType        = USB_DT_ENDPOINT,
	.bEndpointAddress       = USB_DIR_OUT,
	.bmAttributes           = USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_acc_descs[] = {
	(struct usb_descriptor_header *) &acc_interface_desc,
	(struct usb_descriptor_header *) &acc_fullspeed_in_desc,
	(struct usb_descriptor_header *) &acc_fullspeed_out_desc,
	NULL,
};

static struct usb_descriptor_header *hs_acc_descs[] = {
	(struct usb_descriptor_header *) &acc_interface_desc,
	(struct usb_descriptor_header *) &acc_highspeed_in_desc,
	(struct usb_descriptor_header *) &acc_highspeed_out_desc,
	NULL,
};

static struct usb_descriptor_header *ss_acc_descs[] = {
	(struct usb_descriptor_header *) &acc_interface_desc,
	(struct usb_descriptor_header *) &acc_superspeed_in_desc,
	(struct usb_descriptor_header *) &acc_superspeed_comp_desc,
	(struct usb_descriptor_header *) &acc_superspeed_out_desc,
	(struct usb_descriptor_header *) &acc_superspeed_comp_desc,
	NULL,
};

static struct usb_descriptor_header *ssp_acc_descs[] = {
	(struct usb_descriptor_header *) &acc_interface_desc,
	(struct usb_descriptor_header *) &acc_superspeedplus_in_desc,
	(struct usb_descriptor_header *) &acc_superspeedplus_comp_desc,
	(struct usb_descriptor_header *) &acc_superspeedplus_out_desc,
	(struct usb_descriptor_header *) &acc_superspeedplus_comp_desc,
	NULL,
};

static struct usb_string acc_string_defs[] = {
	[INTERFACE_STRING_INDEX].s	= "Android Accessory Interface",
	{  }, /* end of list */
};

static struct usb_gadget_strings acc_string_table = {
	.language		= 0x0409, /* en-US */
	.strings		= acc_string_defs,
};

static struct usb_gadget_strings *acc_strings[] = {
	&acc_string_table,
	NULL,
};

/*****************************************************************************/
/* Accessory Misc Device */

#define misc_to_acc_misc(d) container_of(d, struct acc_misc_dev, misc)

static void acc_getprotocol_work(struct work_struct *data)
{
	struct acc_misc_dev *acc_misc = container_of(data, struct acc_misc_dev,
			getprotocol_work);
	char *envp[2] = { "ACCESSORY=GETPROTOCOL", NULL };

	kobject_uevent_env(&acc_misc->misc.this_device->kobj, KOBJ_CHANGE, envp);
}

static void acc_sendstring_work(struct work_struct *data)
{
	struct acc_misc_dev *acc_misc = container_of(data, struct acc_misc_dev,
			sendstring_work);
	char *envp[2] = { "ACCESSORY=SENDSTRING", NULL };

	kobject_uevent_env(&acc_misc->misc.this_device->kobj, KOBJ_CHANGE, envp);
}

static void acc_start_work(struct work_struct *data)
{
	struct acc_misc_dev *acc_misc = container_of(data, struct acc_misc_dev,
			start_work);
	char *envp[2] = { "ACCESSORY=START", NULL };

	kobject_uevent_env(&acc_misc->misc.this_device->kobj, KOBJ_CHANGE, envp);
}

static void acc_misc_clear_start_requested(struct acc_misc_dev *acc_misc)
{
	unsigned long flags;
	// Is locking necessary?
	spin_lock_irqsave(&acc_misc->lock, flags);
	acc_misc->start_requested = 0;
	spin_unlock_irqrestore(&acc_misc->lock, flags);
}

static void acc_misc_set_start_requested(struct acc_misc_dev *acc_misc)
{
	unsigned long flags;
	// Is locking necessary?
	spin_lock_irqsave(&acc_misc->lock, flags);
	acc_misc->start_requested = 1;
	spin_unlock_irqrestore(&acc_misc->lock, flags);

	schedule_delayed_work(&acc_misc->start_work, msecs_to_jiffies(10));
}

static void acc_misc_get_protocol(struct acc_misc_dev *acc_misc)
{
	unsigned long flags;

	/* clear any string left over from a previous session */
	spin_lock_irqsave(&acc_misc->lock, flags);
	memset(acc_misc->manufacturer, 0, sizeof(acc_misc->manufacturer));
	memset(acc_misc->model, 0, sizeof(acc_misc->model));
	memset(acc_misc->description, 0, sizeof(acc_misc->description));
	memset(acc_misc->version, 0, sizeof(acc_misc->version));
	memset(acc_misc->uri, 0, sizeof(acc_misc->uri));
	memset(acc_misc->serial, 0, sizeof(acc_misc->serial));
	acc_misc->start_requested = 0;
	acc_misc->audio_mode = 0;

	spin_unlock_irqrestore(&acc_misc->lock, flags);
	schedule_work(&acc_misc->getprotocol_work);
}

static void acc_misc_set_connected(struct acc_misc_dev *acc_misc, struct f_acc *acc)
{
	unsigned long flags;

	spin_lock_irqsave(&acc_misc->lock, flags);
	acc_misc->acc_dev = acc;
	acc_misc->connected = true;
	spin_unlock_irqsave(&acc_misc->lock, flags);
}

static void acc_misc_set_disconnected(struct acc_misc_dev *acc_misc)
{
	unsigned long flags;

	spin_lock_irqsave(&acc_misc->lock, flags);
	acc_misc->connected = false;
	acc_misc->acc_dev = NULL;
	spin_unlock_irqsave(&acc_misc->lock, flags);
}

static inline void req_put_locked(struct list_head *head,
	       	struct usb_request *req)
{
	list_add_tail(&req->list, head)
}

static void req_put(struct acc_misc_dev *acc_misc, struct list_head *head,
		struct usb_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&acc_misc->lock, flags);
	req_put_locked(head, req);
	spin_unlock_irqrestore(&acc_misc->lock, flags);
}

static struct usb_request *req_get(struct acc_misc_dev *acc_misc, struct list_head *head)
{
	unsigned long flags;
	struct usb_request *req;

	spin_lock_irqsave(&acc_misc->lock, flags);
	if (list_empty(head)) {
		req = 0;
	} else {
		req = list_first_entry(head, struct usb_request, list);
		list_del(&req->list);
	}
	spin_unlock_irqrestore(&acc_misc->lock, flags);
	return req;
}

static ssize_t acc_misc_read(struct file *fp, char __user *buf,
	size_t count, loff_t *pos)
{
	struct acc_misc_dev *acc_misc = misc_to_acc_misc(fp->private_data);
	struct f_acc *acc;
	struct device *pdev = acc_misc->misc->this_device;
	struct usb_request *req;
	ssize_t r = count;
	ssize_t data_length;
	unsigned int xfer;
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&acc_misc->lock, flags);
	if (!acc_misc->connected) {
		spin_unlock_irqrestore(&acc_misc->lock, flags);
		dev_dbg(pdev, "%s, disconnected", __func__);
		return -ENODEV;
	}

	if (count > BULK_BUFFER_SIZE)
		count = BULK_BUFFER_SIZE;

	/* need to unlock while we wait for an event */
	spin_unlock_irqrestore(&acc_misc->lock, flags);
	
	/* we will block until we're connected */
	dev_dbg(pdev,"%s: waiting for connection\n", __func__);
	ret = wait_event_interruptible(acc_misc->read_wq, acc_misc->connected);
	if (ret < 0) {
		r = ret;
		goto done_unlocked;
	}
	
	spin_lock_irqsave(&acc_misc->lock, flags);
	if (!acc_misc->rx_req[0]) {
		dev_err(pdev, "%s: USB req already handled/freed", __func__);
		r = -EINVAL;
		goto done;
	}

	// Now that we are locked again, make sure we still have a connection.
	acc = acc_misc->acc_dev;
	if (!acc || !acc_misc->connected) {
		dev_err(pdev, "%s: no device connected\n", __func__);
		r = -ENODEV;
		goto done;
	}
	/*
	 * Calculate the data length by considering termination character.
	 * Then compansite the difference of rounding up to
	 * integer multiple of maxpacket size.
	 */
	data_length = count;
	data_length += acc->ep_out->maxpacket - 1;
	data_length -= data_length % acc->ep_out->maxpacket;

	if (acc_misc->rx_done) {
		// last req cancelled. try to get it.
		req = acc_misc->rx_req[0];
		goto copy_data;
	}

requeue_req:
	/* queue a request */
	req = acc_misc->rx_req[0];
	req->length = data_length;
	acc_misc->rx_done = 0; //NTK might need to move to f_acc.
	ret = usb_ep_queue(acc->ep_out, req, GFP_KERNEL);
	if (ret < 0) {
		r = -EIO;
		goto done;
	} else {
		dev_dbg(pdev, "rx %p queue\n", req);
	}

	/* need to unlock while we wait for an event */
	spin_unlock_irqrestore(&acc_misc->lock, flags);

	/* wait for a request to complete */
	ret = wait_event_interruptible(acc_misc->read_wq, acc_misc->rx_done);
	
	spin_lock_irqsave(&acc_misc->lock, flags);
	// Now that we are locked again, make sure we still have a connection.
	acc = acc_misc->acc_dev;
	if (!acc || !acc_misc->connected) {
		// NTK need to revisit: is this the right thing to do?
		dev_err(pdev, "%s: requeue no device connected\n", __func__);
		r = -ENODEV;
		goto done;
	}

	// Ok, we are locked, have a device, and can safely check the ret val
	if (ret < 0) {
		r = ret;
		ret = usb_ep_dequeue(acc->ep_out, req);
		if (ret != 0) {
			// cancel failed. There can be a data already received.
			// it will be retrieved in the next read.
			dev_dbg(pdev, "%s: cancelling failed %d", __func__, ret);
		}
		goto done;
	}

copy_data:
	acc_misc->rx_done = 0;
	if (acc_misc->connected) {
		/* If we got a 0-len packet, throw it back and try again. */
		if (req->actual == 0)
			goto requeue_req;

		dev_dbg(pdev,"rx %p %u\n", req, req->actual);
		xfer = (req->actual < count) ? req->actual : count;
		r = xfer;
		if (copy_to_user(buf, req->buf, xfer))
			r = -EFAULT;
	} else
		r = -EIO;

done:
	spin_unlock_irqrestore(&acc_misc->lock, flags);

done_unlocked:
	return r;
}

static ssize_t acc_misc_write(struct file *fp, const char __user *buf,
	size_t count, loff_t *pos)
{
	struct acc_misc_dev *acc_misc = misc_to_acc_misc(fp->private_data);
	struct f_acc *acc_dev;
	struct device *pdev = acc_misc->misc->this_device;
	struct usb_request *req = 0;
	ssize_t r = count;
	unsigned int xfer;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&acc_lock, flags);
	if (!acc_misc->connected || !acc_misc->open) {
		spin_unlock_irqrestore(&acc_misc->lock, flags);
		dev_dbg(pdev, "%s disconnected", __func__);
		return -ENODEV;
	}

	while (count > 0) {
		/* need to unlock while we wait for an event */
		spin_unlock_irqrestore(&acc_misc->lock, flags);
		/* get an idle tx request to use */
		req = 0;
		ret = wait_event_interruptible(acc_misc->write_wq,
			((req = req_get(acc_misc, &acc_misc->tx_idle)) ||
			 !acc_misc->connected));
		
		spin_lock_irqsave(&acc_lock, flags);
		if (!acc_misc->open || acc_misc->connected) {
			dev_dbg(pdev,
				"%s woken up with resources disconnected\n",
			       	__func__);
			r = -EIO;
			break;
		}

		acc = acc_misc->acc_dev;

		if (!req) {
			r = ret;
			break;
		}

		if (count > BULK_BUFFER_SIZE) {
			xfer = BULK_BUFFER_SIZE;
			/* ZLP, They will be more TX requests so not yet. */
			req->zero = 0;
		} else {
			xfer = count;
			/* If the data length is a multiple of the
			 * maxpacket size then send a zero length packet(ZLP).
			 */
			req->zero = ((xfer % acc->ep_in->maxpacket) == 0);
		}
		if (copy_from_user(req->buf, buf, xfer)) {
			r = -EFAULT;
			break;
		}

		req->length = xfer;
		ret = usb_ep_queue(acc->ep_in, req, GFP_KERNEL);
		if (ret < 0) {
			dev_err(pdev, "%s: xfer error %d\n", __func__, ret);
			r = -EIO;
			break;
		}

		buf += xfer;
		count -= xfer;

		/* zero this so we don't try to free it on error exit */
		req = 0;
	}

	if (req)
		req_put_locked(&acc_misc->tx_idle, req);

	spin_unlock_irqrestore(&acc_misc->lock, flags);
	return r;
}

static long acc_misc_ioctl(struct file *fp, unsigned int code, unsigned long value)
{
	struct acc_misc_dev *acc_misc = misc_to_acc_misc(fp->private_data);
	char *src = NULL;
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&acc_misc->lock, flags);
	switch (code) {
	case ACCESSORY_GET_STRING_MANUFACTURER:
		src = acc_misc->manufacturer;
		break;
	case ACCESSORY_GET_STRING_MODEL:
		src = acc_misc->model;
		break;
	case ACCESSORY_GET_STRING_DESCRIPTION:
		src = acc_misc->description;
		break;
	case ACCESSORY_GET_STRING_VERSION:
		src = acc_misc->version;
		break;
	case ACCESSORY_GET_STRING_URI:
		src = acc_misc->uri;
		break;
	case ACCESSORY_GET_STRING_SERIAL:
		src = acc_misc->serial;
		break;
	case ACCESSORY_IS_START_REQUESTED:
		spin_unlock_irqrestore(&acc_misc->lock, flags);
		return acc_misc->start_requested;
	case ACCESSORY_GET_AUDIO_MODE:
		spin_unlock_irqrestore(&acc_misc->lock, flags);
		return acc_misc->audio_mode;
	}
	if (!src) {
		spin_unlock_irqrestore(&acc_misc->lock, flags);
		return -EINVAL;
	}

	ret = strlen(src) + 1;
	if (copy_to_user((void __user *)value, src, ret))
		ret = -EFAULT;
	
	spin_unlock_irqrestore(&acc_misc->lock, flags);
	return ret;
}

static int acc_misc_open(struct inode *ip, struct file *fp)
{
	struct acc_misc_dev *acc_misc = misc_to_acc_misc(fp->private_data);
	unsigned long flags;

	spin_lock_irqsave(&acc_misc->lock, flags);
	
	if (acc_misc->open == true) {
		spin_unlock_irqrestore(&acc_misc->lock, flags);
		return -EBUSY;
	}
	
	acc_misc->open = true;
	
	spin_unlock_irqrestore(&acc_misc->lock, flags);
	return 0;
}

static int acc_misc_release(struct inode *ip, struct file *fp)
{
	struct acc_misc_dev *acc_misc = misc_to_acc_misc(fp->private_data);
	unsigned long flags;

	spin_lock_irqsave(&acc_misc->lock, flags);
	
	if (acc_misc->open == false) {
		dev_err(&acc_misc->misc->this_device, "%s: was not open?", __func__);
	} else
		acc_misc->open = false;

	spin_unlock_irqrestore(&acc_misc->lock, flags);
	return 0;
}

/* file operations for /dev/usb_accessory */
static const struct file_operations acc_misc_fops = {
	.owner = THIS_MODULE,
	.read = acc_misc_read,
	.write = acc_misc_write,
	.unlocked_ioctl = acc_misc_ioctl,
	.compat_ioctl = acc_misc_ioctl,
	.open = acc_misc_open,
	.release = acc_misc_release,
};

/*****************************************************************************/

/*
static struct f_acc *get_acc_dev(void)
{
	struct f_acc_ref *ref = &_acc_dev_ref;

	return kref_get_unless_zero(&ref->kref) ? ref->acc_dev : NULL;
}

static void __put_acc_dev(struct kref *kref)
{
	struct f_acc_ref *ref = container_of(kref, struct acc_dev_ref, kref);
	struct f_acc *dev = ref->acc_dev;

	// Cancel any async work 
	cancel_delayed_work_sync(&dev->start_work);
	cancel_work_sync(&dev->getprotocol_work);
	cancel_work_sync(&dev->sendstring_work);

	acc_hid_cleanup(&dev->hid_opts);

	ref->acc_dev = NULL;
	kfree(dev);
}

static void put_acc_dev(struct f_acc *dev)
{
	struct f_acc_ref *ref = dev->ref;

	WARN_ON(ref->acc_dev != dev);
	kref_put(&ref->kref, __put_acc_dev);
}
*/
static inline struct f_acc *func_to_acc(struct usb_function *f)
{
	return container_of(f, struct f_acc, function);
}

static struct usb_request *acc_request_new(struct usb_ep *ep, int buffer_size)
{
	struct usb_request *req = usb_ep_alloc_request(ep, GFP_KERNEL);

	if (!req)
		return NULL;

	/* now allocate buffers for the requests */
	req->buf = kmalloc(buffer_size, GFP_KERNEL);
	if (!req->buf) {
		usb_ep_free_request(ep, req);
		return NULL;
	}

	return req;
}

static void acc_request_free(struct usb_request *req, struct usb_ep *ep)
{
	if (req) {
		kfree(req->buf);
		usb_ep_free_request(ep, req);
	}
}

static int create_bulk_endpoints(struct f_acc *acc,
				struct usb_endpoint_descriptor *in_desc,
				struct usb_endpoint_descriptor *out_desc)
{
	struct usb_composite_dev *cdev = acc->cdev;
	struct usb_request *req;
	struct usb_ep *ep;
	int i;

	ep = usb_ep_autoconfig(cdev->gadget, in_desc);
	if (!ep) {
		dev_err(acc->misc->misc->this_device,
				"usb_ep_autoconfig for ep_in failed\n");
		return -ENODEV;
	}
	ep->driver_data = acc; /* claim the endpoint */
	acc->ep_in = ep;

	ep = usb_ep_autoconfig(cdev->gadget, out_desc);
	if (!ep) {
		dev_err(acc->misc->misc->this_device,
			       "usb_ep_autoconfig for ep_out failed\n");
		return -ENODEV;
	}
	ep->driver_data = acc;		/* claim the endpoint */
	acc->ep_out = ep;

	// NTK TODO: reqs were moved to acc_misc... update.
	/* now allocate requests for our endpoints */
	for (i = 0; i < TX_REQ_MAX; i++) {
		req = acc_request_new(acc->ep_in, BULK_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->context = acc;
		req->complete = acc_complete_in;
		req_put(acc, &acc->tx_idle, req);
	}
	for (i = 0; i < RX_REQ_MAX; i++) {
		req = acc_request_new(acc->ep_out, BULK_BUFFER_SIZE);
		if (!req)
			goto fail;
		req->context = acc;
		req->complete = acc_complete_out;
		acc->rx_req[i] = req;
	}

	return 0;

fail:
	dev_err(&acc->misc->misc->this_device,
			"%s could not allocate requests\n", __func__);
	while ((req = req_get(acc, &acc->tx_idle)))
		acc_request_free(req, acc->ep_in);
	for (i = 0; i < RX_REQ_MAX; i++) {
		acc_request_free(acc->rx_req[i], dev->ep_out);
		acc->rx_req[i] = NULL;
	}

	return -1;
}

static void acc_complete_set_string(struct usb_ep *ep, struct usb_request *req)
{
	struct f_acc *acc = ep->driver_data;
	char *string_dest = NULL;
	int length = req->actual;

	if (req->status != 0) {
		//has bind been called to access cdev?	
		dev_err(acc->misc->misc->this_device, "%s, err %d\n", __func__,
				req->status);
		return;
	}
	// NTK TODO: Does it make sense for the strings to be in acc, misc, or opts?
	switch (acc->string_index) {
	case ACCESSORY_STRING_MANUFACTURER:
		string_dest = acc->manufacturer;
		break;
	case ACCESSORY_STRING_MODEL:
		string_dest = acc->model;
		break;
	case ACCESSORY_STRING_DESCRIPTION:
		string_dest = acc->description;
		break;
	case ACCESSORY_STRING_VERSION:
		string_dest = acc->version;
		break;
	case ACCESSORY_STRING_URI:
		string_dest = acc->uri;
		break;
	case ACCESSORY_STRING_SERIAL:
		string_dest = acc->serial;
		break;
	}
	if (string_dest) {
		unsigned long flags;

		if (length >= ACC_STRING_SIZE)
			length = ACC_STRING_SIZE - 1;

		spin_lock_irqsave(&acc->lock, flags);
		memcpy(string_dest, req->buf, length);
		/* ensure zero termination */
		string_dest[length] = 0;
		spin_unlock_irqrestore(&acc->lock, flags);
	} else {
		dev_err(acc->misc->misc->this_device, "unknown accessory string index %d\n",
			acc->string_index);
	}
}

static void acc_complete_in(struct usb_ep *ep, struct usb_request *req)
{
	struct f_acc *acc = req->context;

	if (!acc)
		return;

	if (req->status == -ESHUTDOWN) {
		dev_dbg(&acc->misc->misc->this_device, "%s set disconnected\n",
				__func__);
		acc_misc_set_disconnected(acc->misc);
	}

	req_put(acc->misc, &acc->misc->tx_idle, req);

	wake_up(&acc->write_wq);
}

static void acc_complete_out(struct usb_ep *ep, struct usb_request *req)
{
	struct f_acc *acc = req->context;

	if (!acc)
		return;
	// NTK TODO, need locking?
	acc->misc->rx_done = 1;
	if (req->status == -ESHUTDOWN) {
		dev_dbg(&acc->misc->misc->this_device, "%s set disconnected",
			       	__func__);
		acc_misc_set_disconnected(acc->misc);
	}

	wake_up(&acc->misc->read_wq);
}


static void acc_complete_setup_noop(struct usb_ep *ep, struct usb_request *req)
{
	/*
	 * Default no-op function when nothing needs to be done for the
	 * setup request
	 */
}

static int acc_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct usb_string *us;
	struct f_acc *acc = func_to_acc(f);
	int id;
	int ret;

	us = usb_gstrings_attach(cdev, acc_strings, ARRAY_SIZE(acc_string_defs));
	if (IS_ERR(us))
		return PTR_ERR(us);
	ret = us[INTERFACE_STRING_INDEX].id;
	acc_interface_desc.iInterface = ret;
	acc->cdev = c->cdev;

	ret = acc_hid_bind(&acc->hid_opts);
	if (ret)
		return ret;

	acc_misc_clear_start_requested(acc->misc);

	/* allocate interface ID(s) */
	id = usb_interface_id(c, f);
	if (id < 0)
		return id;
	acc_interface_desc.bInterfaceNumber = id;


	//TODO: dynamic allocation?

	/* allocate endpoints */
	ret = create_bulk_endpoints(acc, &acc_fullspeed_in_desc,
			&acc_fullspeed_out_desc);
	if (ret)
		return ret;

	/* support high speed hardware */
	acc_highspeed_in_desc.bEndpointAddress =
		acc_fullspeed_in_desc.bEndpointAddress;
	acc_highspeed_out_desc.bEndpointAddress =
		acc_fullspeed_out_desc.bEndpointAddress;

	/* support super speed hardware */
	acc_superspeed_in_desc.bEndpointAddress =
		acc_fullspeed_in_desc.bEndpointAddress;
	acc_superspeed_out_desc.bEndpointAddress =
		acc_fullspeed_out_desc.bEndpointAddress;

	/* support super speed plus hardware */
	acc_superspeedplus_in_desc.bEndpointAddress =
		acc_fullspeed_in_desc.bEndpointAddress;
	acc_superspeedplus_out_desc.bEndpointAddress =
		acc_fullspeed_out_desc.bEndpointAddress;

	return 0;
}

static void acc_unbind(struct usb_configuration *c,
		struct usb_function *f)
{
	struct f_acc *acc = func_to_acc(f);
	struct acc_misc_dev *acc_misc = acc->misc; 
	struct usb_request *req;
	int i;

	acc_misc_set_disconnected(acc_misc);
	wake_up(&acc_misc->read_wq);	/* unblock reads on closure */
	wake_up(&acc_misc->write_wq);	/* likewise for writes */

	while ((req = req_get(acc_misc, &acc_misc->tx_idle)))
		acc_request_free(req, acc->ep_in);
	for (i = 0; i < RX_REQ_MAX; i++) {
		acc_request_free(acc_misc->rx_req[i], acc->ep_out);
		acc_misc->rx_req[i] = NULL;
	}

	acc_hid_unbind(&acc->hid_opts);
}

static int acc_set_alt(struct usb_function *f,
		unsigned int intf, unsigned int alt)
{
	struct f_acc *acc = func_to_acc(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	int ret;

	ret = config_ep_by_speed(cdev->gadget, f, acc->ep_in);
	if (ret)
		return ret;

	ret = usb_ep_enable(acc->ep_in);
	if (ret)
		return ret;

	ret = config_ep_by_speed(cdev->gadget, f, acc->ep_out);
	if (ret)
		return ret;

	ret = usb_ep_enable(acc->ep_out);
	if (ret) {
		usb_ep_disable(acc->ep_in);
		return ret;
	}

	acc_misc_set_connected(acc->misc);

	/* readers may be blocked waiting for us to go online */
	wake_up(&acc->misc->read_wq);
	return 0;
}

static void acc_disable(struct usb_function *f)
{
	struct f_acc *acc = func_to_acc(f);
	// NTK should this be after ep_disable?
	acc_misc_set_disconnected(acc->misc);
	usb_ep_disable(acc->ep_in);
	usb_ep_disable(acc->ep_out);
	wake_up(&acc->misc->read_wq);
}

void acc_disconnect(void)
{
	struct f_acc *acc = get_acc_dev();

	if (!acc)
		return;

	acc_hid_disconnect(&acc->hid_opts);
	put_acc_dev(acc);
}
EXPORT_SYMBOL_GPL(acc_disconnect);

static void acc_cleanup(void)
{
	struct f_acc *acc = get_acc_dev();

	misc_deregister(&acc_device);
	put_acc_dev(acc);
	put_acc_dev(acc); /* Pairs with kref_init() in acc_init() */
}
static struct f_acc_opts *to_f_acc_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_acc_opts,
		func_inst.group);
}

static void acc_attr_release(struct config_item *item)
{
	struct f_acc_opts *acc_opts = to_f_acc_opts(item);

	usb_put_function_instance(&acc_opts->func_inst);
	// NTK TODO: do I need to free something here?
	// unset opts?
}

static struct configfs_item_operations acc_item_ops = {
	.release        = acc_attr_release,
};

static struct config_item_type acc_func_type = {
	.ct_item_ops    = &acc_item_ops,
	.ct_owner       = THIS_MODULE,
};

static struct f_acc_opts *to_acc_opts(struct usb_function_instance *fi)
{
	return container_of(fi, struct f_acc_opts, func_inst);
}

static int acc_set_inst_name(struct usb_function_instance *fi, const char *name)
{
	struct f_acc_opts *acc_opts;
	char *ptr;
	int name_len;

	name_len = strlen(name) + 1;
	if (name_len > MAX_INST_NAME_LEN)
		return -ENAMETOOLONG;

	ptr = kstrndup(name, name_len, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	acc_opts = to_acc_opts(fi);
	acc_opts->name = ptr;
	return 0;
}

static void acc_free_inst(struct usb_function_instance *fi)
{
	struct f_acc_opts *acc_opts;

	acc_opts = to_acc_opts(fi);
	kfree(acc_opts->name);
	acc_cleanup();
}

static struct usb_function_instance *acc_alloc_inst(void)
{
	struct f_acc_opts *acc_opts;
	int err;

	acc_opts = kzalloc(sizeof(*acc_opts), GFP_KERNEL);
	if (!acc_opts)
		return ERR_PTR(-ENOMEM);

	acc_opts->func_inst.set_inst_name = acc_set_inst_name;
	acc_opts->func_inst.free_func_inst = acc_free_inst;

	if (!g_acc_misc) {
		struct miscdevice *misc;
		g_acc_misc = kzalloc(sizeof(*g_acc_misc), GFP_KERNEL);
		if (!g_acc_misc)
			return -ENOMEM;

		spin_lock_init(&g_acc_misc->lock);
		init_waitqueue_head(&g_acc_misc->read_wq);
		init_waitqueue_head(&g_acc_misc->write_wq);
		INIT_LIST_HEAD(&g_acc_misc->tx_idle);
		INIT_DELAYED_WORK(&g_acc_misc->start_work, acc_start_work);
		INIT_WORK(&g_acc_misc->getprotocol_work, acc_getprotocol_work);
		INIT_WORK(&g_acc_misc->sendstring_work, acc_sendstring_work);

		misc = &g_acc_misc->misc;
		misc->minor = MISC_DYNAMIC_MINOR;
		misc->name = ACCESSORY_MISC_DEV_NAME;
		misc->fops = &acc_misc_fops;

		ret = misc_register(misc);
		if (ret) {
			kfree(g_misc_dev);
			return ret;
		}

		kref_init(&g_acc_misc->refcount);
	} else {
		kref_get(&g_acc_misc->refcount);
	}

	acc_opts->misc = g_misc_dev;

	// NTK TODO this might need to be moved to alloc func.
	ret = acc_hid_init(&acc_opts->hid_opts);
	if (ret)
		//NTK TODO cleanup.

	config_group_init_type_name(&acc_opts->func_inst.group,
					"", &acc_func_type);
	return  &acc_opts->func_inst;
}

static void acc_free_func(struct usb_function *f)
{
	struct f_acc *acc = func_to_acc(f);

	put_acc_dev(acc);
}

bool __acc_req_match(const struct usb_ctrlrequest *ctrl)
{
	u8 bRequestType = ctrl->bRequestType;
	u8 bRequest = ctrl->bRequest;
	bool ret = false;

	if (bRequestType == (USB_DIR_OUT | USB_TYPE_VENDOR)) {
		switch(bRequest) {
			case ACCESSORY_START:
			case ACCESSORY_SEND_STRING:
			case ACCESSORY_SET_AUDIO_MODE:
			case ACCESSORY_REGISTER_HID:
			case ACCESSORY_UNREGISTER_HID:
			case ACCESSORY_SET_HID_REPORT_DESC:
			case ACCESSORY_SEND_HID_EVENT:
				ret = true;
				break;
			default:
				ret = false;
		}
	} else if ((bRequestType == (USB_DIR_IN | USB_TYPE_VENDOR)) &&
		       	(bRequest == ACCESSORY_GET_PROTOCOL))
		ret = true;

	if (ret == false)
		ret = acc_hid_req_match(ctrl);
	return ret;
}

static bool acc_req_match(struct usb_function *f,
		const struct usb_ctrlrequest *ctrl, bool config)
{
	return __acc_req_match(ctrl);
}

bool acc_req_match_composite( struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl)
{
	return __acc_req_match(ctrl);
}
EXPORT_SYMBOL_GPL(acc_req_match_composite);

static int __acc_setup(struct usb_composite_dev *cdev,
		const struct usb_ctrlrequest *ctrl)
{
	struct f_acc *acc = get_acc_dev();
	int value = -EOPNOTSUPP;
	u8 bRequestType = ctrl->bRequestType;
	u8 bRequest = ctrl->bRequest;
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wLength = le16_to_cpu(ctrl->wLength);

	/*
	 * If instance is not created which is the case in power off charging
	 * mode, dev will be NULL. Hence return error if it is the case.
	 */
	if (!acc)
		return -ENODEV;

	if (bRequestType == (USB_DIR_OUT | USB_TYPE_VENDOR)) {
		if (bRequest == ACCESSORY_START) {
			acc_misc_set_start_requested(acc->misc); //NTK TODO get acc_misc...
			value = 0;
			cdev->req->complete = acc_complete_setup_noop;
		} else if (bRequest == ACCESSORY_SEND_STRING) {
			schedule_work(&acc->misc->sendstring_work);
			acc->misc->string_index = wIndex;
			cdev->gadget->ep0->driver_data = acc->misc;
			cdev->req->complete = acc_complete_set_string;
			value = wLength;
		} else if (bRequest == ACCESSORY_SET_AUDIO_MODE &&
				wIndex == 0 && wLength == 0) {
			acc->misc->audio_mode = wValue;
			cdev->req->complete = acc_complete_setup_noop;
			value = 0;
		}
	} else if (bRequestType == (USB_DIR_IN | USB_TYPE_VENDOR)) {
		if (bRequest == ACCESSORY_GET_PROTOCOL) {
			*((u16 *)cdev->req->buf) = PROTOCOL_VERSION;
			value = sizeof(u16);
			cdev->req->complete = acc_complete_setup_noop;
			acc_misc_get_protocol(acc->misc);
		}
	}

	if (value >= 0) {
		cdev->req->zero = 0;
		cdev->req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, cdev->req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "%s setup response queue error\n",
					__func__);
	} else if (value == -EOPNOTSUPP)
		value = acc_hid_setup(&acc->opts.hid_opts, cdev, ctrl);

	if (value == -EOPNOTSUPP)
		dev_dbg(dev->misc->misc->this_device,,
			"unknown class-specific control req %02x.%02x v%04x i%04x l%u\n",
			ctrl->bRequestType, ctrl->bRequest,
			wValue, wIndex, wLength);
//	put_acc_dev(acc);
	return value;
}

int acc_setup_composite(struct usb_composite_dev *cdev,
			      const struct usb_ctrlrequest *ctrl)
{
	return __acc_setup(cdev, ctrl);
}
EXPORT_SYMBOL_GPL(acc_setup_composite);

static int acc_setup(struct usb_function *f,
			const struct usb_ctrlrequest *ctrl)
{
	if (f->config != NULL && f->config->cdev != NULL)
		return __acc_setup(f->config->cdev, ctrl);
	else
		return -1;
}

static struct usb_function *acc_alloc(struct usb_function_instance *fi)
{
	struct f_acc *acc;
	struct f_acc_opts *opts = container_of(fi, struct f_acc_opts, func_inst);

	acc = kzalloc(sizeof(*acc), GFP_KERNEL);
	if (!acc)
		return ERR_PTR(-ENOMEM);

	// NTK TODO: is this lock necessary?
	spin_lock_init(&acc->lock);

	acc->function.name = "accessory";

	//Maybe move these to bind?
	acc->function.strings = acc_strings;
	acc->function.fs_descriptors = fs_acc_descs;
	acc->function.hs_descriptors = hs_acc_descs;
	acc->function.ss_descriptors = ss_acc_descs;
	acc->function.ssp_descriptors = ssp_acc_descs;
	acc->function.bind = acc_bind;
	acc->function.unbind = acc_unbind;
	acc->function.set_alt = acc_set_alt;
	acc->function.disable = acc_disable;
	acc->function.free_func = acc_free_func;
	acc->function.req_match = acc_req_match;
	acc->function.setup = acc_setup;

	acc->opts = opts;
	opts->acc_dev = acc;

	return &dev->function;
}
DECLARE_USB_FUNCTION_INIT(accessory, acc_alloc_inst, acc_alloc);
MODULE_LICENSE("GPL");
