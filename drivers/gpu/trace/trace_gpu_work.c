// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPU memory trace points
 *
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/gpu_work.h>

EXPORT_TRACEPOINT_SYMBOL(gpu_kick);
EXPORT_TRACEPOINT_SYMBOL(gpu_queue_full);
