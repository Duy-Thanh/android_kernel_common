/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 */

#ifndef _TRUSTY_LOGBUFFER_H_
#define _TRUSTY_LOGBUFFER_H_

#include <linux/miscdevice.h>
#include <linux/seq_file.h>

#define ID_LENGTH 50

/**
 * struct trusty_logbuffer_config - logbuffer misc device configuration
 *
 * @dev:     trusty-log device (used for naming/logging purpose)
 * @name:    name postfix used for the misc device instance name
 *           following the convention "logbuffer_<name>_<id>"
 * @id:      unique identifier used for the misc device instance name
 *           following the convention "logbuffer_<name>_<id>"
 * @seq_ops: callback functions as defined by &struct seq_operations
 *           for seq_file read iteration
 */
struct trusty_logbuffer_config {
	struct device *dev;
	char name[16];
	int id;
	struct seq_operations seq_ops;
};

/**
 * struct trusty_logbuffer_state - logbuffer misc device state
 *
 * @cfg:    misc device configuration to be initialised by the client
 * @misc:   misc device created for the logbuffer virtual file
 * @name:   misc device instance name fllowing the convention
 *          "logbuffer_<dev.name>_<postfix>"
 */
struct trusty_logbuffer {
	struct trusty_logbuffer_config cfg;
	struct miscdevice misc;
	struct seq_file sfile;
	char device_name[64];
};

/**
 * trusty_logbuffer_register() - Register a new log buffer as a misc device
 *
 * @instance: struct trusty_logbuffer, with initialised cfg.
 *
 * Return:
 * 0 if successfull, negative error code otherwise.
 */
int trusty_logbuffer_register(struct trusty_logbuffer *instance);

/**
 * trusty_logbuffer_unregister() - Unregister a log buffer
 * @instance: logbuffer instance to unregister
 *
 */
void trusty_logbuffer_unregister(struct trusty_logbuffer *instance);

#endif /* _TRUSTY_LOGBUFFER_H_ */
