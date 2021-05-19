/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC
 */

#ifndef _TRUSTY_LOGBUFFER_H_
#define _TRUSTY_LOGBUFFER_H_

#include <linux/seq_file.h>

struct trusty_logbuffer;

/**
 * trusty_logbuffer_register() - Register a new log buffer as a misc device
 *
 * @name:   used to construct the log file name as "/dev/logbuffer_<name>".
 * @dev:    trusty-log device (used for logging purpose)
 * @ctx:    client context to be passed to the show callback
 * @show:   callback function implemented by the client.
 *          This callback is invoked when the seq_file's show function
 *          is invoked
 *
 * Return:
 * the pointer to the logbuffer context.
 */
struct trusty_logbuffer *trusty_logbuffer_register(
	const char *name, struct device *dev, void *ctx,
	int (*show)(void *ctx, struct seq_file *s, void *data));

/**
 * trusty_logbuffer_unregister() - Unregister a log buffer
 * @instance: logbuffer instance to unregister
 *
 */
void trusty_logbuffer_unregister(struct trusty_logbuffer *instance);

#endif /* _TRUSTY_LOGBUFFER_H_ */
