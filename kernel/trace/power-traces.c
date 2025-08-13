// SPDX-License-Identifier: GPL-2.0
/*
 * Power trace points
 *
 * Copyright (C) 2009 Arjan van de Ven <arjan@linux.intel.com>
 */

#include <linux/string.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/module.h>

#define CREATE_TRACE_POINTS
#include <trace/events/power.h>

EXPORT_TRACEPOINT_SYMBOL_GPL(suspend_resume);
EXPORT_TRACEPOINT_SYMBOL_GPL(cpu_idle);
EXPORT_TRACEPOINT_SYMBOL_GPL(cpu_frequency);
<<<<<<< HEAD   (6ee0ad4f265f287f9a090e4da238b6abadb9e1c7 Merge 4ff261e725d7 ("Merge tag 'trace-rv-6.17' of git://git.)
EXPORT_TRACEPOINT_SYMBOL_GPL(powernv_throttle);
EXPORT_TRACEPOINT_SYMBOL_GPL(device_pm_callback_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(device_pm_callback_end);
||||||| BASE   (4ff261e725d7376c12e745fdbe8a33cd6dbd5a83 Merge tag 'trace-rv-6.17' of git://git.kernel.org/pub/scm/li)
EXPORT_TRACEPOINT_SYMBOL_GPL(powernv_throttle);

=======

>>>>>>> BRANCH (2be6a7503d32eb1d60b4c9c15547a10d4ec9a934 Merge tag 'trace-unused-v6.17' of git://git.kernel.org/pub/s)
