/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2024 Google LLC
 */
#ifndef __U_ACCESSORY_H
#define __U_ACCESSORY_H
#include <linux/miscdevice.h>

#define ACC_STRING_SIZE 256

/* number of tx and rx requests to allocate */
#define TX_REQ_MAX 4
#define RX_REQ_MAX 2

struct acc_misc_dev {
	struct kref refcount;
	struct miscdevice misc;
	struct f_acc *acc_dev;
	spinlock_t lock;
	bool open; // Open is set when the misc file is opened.
	bool connected; //Connected is set when the usb connection is active

	/* set to 1 if we have a pending start request */
	int start_requested;
	int audio_mode;

	// NTK likely should move back to f_acc.
	/* strings sent by the host */
	char manufacturer[ACC_STRING_SIZE];
	char model[ACC_STRING_SIZE];
	char description[ACC_STRING_SIZE];
	char version[ACC_STRING_SIZE];
	char uri[ACC_STRING_SIZE];
	char serial[ACC_STRING_SIZE];

	wait_queue_head_t read_wq;
	wait_queue_head_t write_wq;
	struct list_head tx_idle;
	struct usb_request *rx_req[RX_REQ_MAX];
	int rx_done;

	/* delayed work for handling ACCESSORY_START */
	struct delayed_work start_work;

	/* work for handling ACCESSORY GET PROTOCOL */
	struct work_struct getprotocol_work;

	/* work for handling ACCESSORY SEND STRING */
	struct work_struct sendstring_work;
}

struct f_acc {
	struct usb_function function;
	struct usb_composite_dev *cdev;
	spinlock_t lock; //NTK is this necessary?

	struct usb_ep *ep_in;
	struct usb_ep *ep_out;

	/* for acc_complete_set_string */
	int string_index;
	struct f_acc_opts *opts;
	struct acc_misc_dev *misc;
};

struct f_acc_opts {
	struct usb_function_instance func_inst;
	struct f_acc *acc_dev;
	struct acc_misc_dev *misc;
	struct acc_hid_opts hid_opts;
	const char *name;
};

#endif /* __UACCESSORY_H  */
