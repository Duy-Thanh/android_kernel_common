/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2022 Google, Inc.
 *
 * This header file contains the definitions of APIs, used for the
 * registration/unregistration of shared-memory used for the
 * exchange of info between the Linux Trusty-Driver and the Trusty-Kernel.
 */
#ifndef _TRUSTY_SHARE_API_H_
#define _TRUSTY_SHARE_API_H_

#include <linux/device.h>

extern void *trusty_register_share(struct device *device);
extern int trusty_unregister_share(void *share_state);

#endif /* _TRUSTY_SHARE_API_H_ */
