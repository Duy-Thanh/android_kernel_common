// SPDX-License-Identifier: GPL-2.0
/*
 * GPU trace points
 *
 * Copyright (C) 2013-2019 Google, Inc.
 */

#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/gpu.h>

EXPORT_TRACEPOINT_SYMBOL(gpu_sched_enqueue);
EXPORT_TRACEPOINT_SYMBOL(gpu_sched_submit);
EXPORT_TRACEPOINT_SYMBOL(gpu_sched_complete);
EXPORT_TRACEPOINT_SYMBOL(gpu_freq);
