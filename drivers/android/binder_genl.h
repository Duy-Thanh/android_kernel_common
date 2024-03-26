/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024 Google, Inc.
 */

#ifndef _LINUX_BINDER_GENL_H
#define _LINUX_BINDER_GENL_H

#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/genetlink.h>
#include <uapi/linux/android/binder.h>

#include "binder_internal.h"

int binder_init_genl(void);

bool binder_report_enabled(struct binder_context *context, u32 mask);

void binder_send_report(struct binder_report *report, int len);

#endif /* _LINUX_BINDER_GENL_H */