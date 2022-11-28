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

struct trusty_share_state;
int trusty_get_requested_nice(unsigned int cpu_num, struct trusty_share_state *tcpu_state);
int trusty_set_actual_nice(unsigned int cpu_num, struct trusty_share_state *tcpu_state, int nice);

#endif /* _TRUSTY_SHARE_API_H_ */
