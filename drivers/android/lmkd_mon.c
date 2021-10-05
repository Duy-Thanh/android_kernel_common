// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/oom.h>
#include <linux/psi.h>
#include <linux/timer.h>
#include <trace/hooks/psi.h>

#define LMKD_TRIGGER_COUNT	3

static int is_active = 0;
static int lmkd_pid = -1;
static int wd_timeout_ms = 2000;
static DEFINE_SPINLOCK(wd_lock);
static struct timer_list wdog_timer;
static struct psi_trigger *lmkd_triggers[LMKD_TRIGGER_COUNT];

static void watchdog_fn(struct timer_list *t)
{
	const gfp_t gfp_mask = GFP_KERNEL;
	struct oom_control oc = {
		.zonelist = node_zonelist(first_memory_node, gfp_mask),
		.nodemask = NULL,
		.memcg = NULL,
		.gfp_mask = gfp_mask,
		.order = -1,
	};

	pr_info("LMKD watchdog fired, generating oom-kill!\n");
	mutex_lock(&oom_lock);
	if (!out_of_memory(&oc))
		pr_warn("OOM request ignored. No task eligible\n");
	mutex_unlock(&oom_lock);

	spin_lock(&wd_lock);
	/* Re-activate the timer until lmkd disables it */
	if (is_active)
		mod_timer(&wdog_timer,
			  jiffies + msecs_to_jiffies(wd_timeout_ms));
	spin_unlock(&wd_lock);
}

static void set_timer(int active)
{
	spin_lock(&wd_lock);
	if (active != is_active) {
		if (active)
			mod_timer(&wdog_timer,
				  jiffies + msecs_to_jiffies(wd_timeout_ms));
		else
			del_timer_sync(&wdog_timer);
		is_active = active;
	}
	spin_unlock(&wd_lock);
}

static void on_psi_event(void *data, struct psi_trigger *t)
{
	int i;

	for (i = 0; i < LMKD_TRIGGER_COUNT; i++) {
		if (lmkd_triggers[i] == t) {
			set_timer(1);
			break;
		}
	}
}

static void on_psi_arm_trigger(void *data, struct task_struct *task,
			       struct psi_trigger *t)
{
	if (task->pid == lmkd_pid) {
		int i;

		for (i = 0; i < LMKD_TRIGGER_COUNT; i++) {
			if (lmkd_triggers[i] == t) {
				set_timer(0);
				break;
			}
			if (!lmkd_triggers[i]) {
				/* New trigger, start monitoring */
				lmkd_triggers[i] = t;
				break;
			}
		}
	}
}

static int lmkd_pid_set(const char *val, const struct kernel_param *kp)
{
	long pid;
	int ret;

	if (!val)
		return -EINVAL;

	ret = kstrtol(val, 10, &pid);
	if (ret != 0 || pid <= 0)
		return -EINVAL;

	ret = param_set_int(val, kp);

	/* Reset previous trigger array */
	if (!ret)
		memset(lmkd_triggers, 0, sizeof(lmkd_triggers));

	return ret;
}

static int active_set(const char *val, const struct kernel_param *kp)
{
	long activate;
	int ret;

	if (!val)
		return -EINVAL;

	ret = kstrtol(val, 10, &activate);
	if (ret != 0 || activate < 0 || activate > 1)
		return -EINVAL;

	set_timer(activate);

	return 0;
}

static const struct kernel_param_ops lmkd_pid_ops = {
	.set = lmkd_pid_set,
	.get = param_get_int,
};

static const struct kernel_param_ops active_ops = {
	.set = active_set,
	.get = param_get_int,
};

module_param_cb(pid, &lmkd_pid_ops, &lmkd_pid, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(pid, "PID of the LMKD process");

module_param_cb(active, &active_ops, &is_active, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(active, "Watchdor active flag");

module_param_named(timeout_ms, wd_timeout_ms, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(timeout_ms, "Timeout for LMKD to process a PSI event");

static int __init lmkd_mon_init(void)
{
	int ret;

	ret = register_trace_android_vh_psi_event(on_psi_event, NULL);
	if (ret)
		return ret;

	ret = register_trace_android_vh_psi_arm_trigger(on_psi_arm_trigger,
							NULL);
	if (ret)
		return ret;

	timer_setup(&wdog_timer, watchdog_fn, TIMER_DEFERRABLE);

	return 0;
}
static void __exit lmkd_mon_exit(void)
{
	del_timer_sync(&wdog_timer);
}

module_init(lmkd_mon_init);
module_exit(lmkd_mon_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("LMKD monitor driver");
