// SPDX-License-Identifier: GPL-2.0-only
/* vendor_hook.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2020 Google LLC
 */

#define CREATE_TRACE_POINTS
#include <trace/hooks/vendor_hooks.h>
<<<<<<< HEAD   (026b81 BACKPORT: kthread: Do not preempt current task if it is goin)
#include <trace/hooks/dtask.h>
#include <trace/hooks/net.h>
=======
#include <trace/hooks/sched.h>
>>>>>>> CHANGE (a1fc1f ANDROID: sched: add restrict vendor hook to modify task plac)

/*
 * Export tracepoints that act as a bare tracehook (ie: have no trace event
 * associated with them) to allow external modules to probe them.
 */
<<<<<<< HEAD   (026b81 BACKPORT: kthread: Do not preempt current task if it is goin)
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_mutex_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_mutex_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_read_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_read_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_write_wait_start);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_rwsem_write_wait_finish);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_sched_show_task);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_ptype_head);
EXPORT_TRACEPOINT_SYMBOL_GPL(android_vh_kfree_skb);

=======
EXPORT_TRACEPOINT_SYMBOL_GPL(android_rvh_select_task_rq_fair);
>>>>>>> CHANGE (a1fc1f ANDROID: sched: add restrict vendor hook to modify task plac)
