// SPDX-License-Identifier: GPL-2.0
/* memhealth.c
 *
 * Copyright (C) 2023 Google, Inc.
 */

#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <trace/events/oom.h>

#define MEMHEALTH_DIRECTORY "mem-health"
#define OOM_KILL_LIST_ENTRY "oom_kill_list"

static DEFINE_SPINLOCK(memhealth_lock);
static struct list_head oom_kill_list;	/* Keep track of OOM killer victims */
static wait_queue_head_t memhealth_wq;

struct oom_kill_data {
	pid_t pid;
	uid_t uid;
	char process_name[TASK_COMM_LEN];
	unsigned long timestamp_ms;
	struct list_head list;
};

static void add_oom_kill_to_list(pid_t pid, unsigned long timestamp)
{
	struct oom_kill_data *new_node;
	struct task_struct *task;
	const struct cred *cred;
	struct pid *pid_struct;

	/**
	* Utilize `GFP_ATOMIC` to ensure our mem allocation
	* will not be interrupted
	*/
	new_node = kmalloc(sizeof(*new_node), GFP_ATOMIC);
	if (!new_node) {
		printk(KERN_ERR "memhealth failed to allocate memory for oom_kill new node\n");
		return;
	}

	pid_struct = find_get_pid(pid);
	if (!pid_struct) {
		printk(KERN_ERR "memhealth failed to get pid %d\n", pid);
		return;
	}

	task = get_pid_task(pid_struct, PIDTYPE_PID);
	put_pid(pid_struct);
	if (!task) {
		printk(KERN_ERR "memhealth failed to find process with pid %d \n", pid);
		return;
	}

	cred = get_task_cred(task);
	if (!cred) {
		printk(KERN_ERR "memhealth received error getting cred from task\n");
		return;
	}

	if (strscpy_pad(new_node->process_name, task->comm, TASK_COMM_LEN) < 0) {
		printk(KERN_ERR "memhealth failed to copy process name to new oom victim node\n");
		put_task_struct(task);
		put_cred(cred);
		return;
	}

	put_task_struct(task);

	new_node->pid = pid;
	new_node->timestamp_ms = timestamp;
	new_node->uid = cred->uid.val;

	put_cred(cred);

	spin_lock(&memhealth_lock);
	list_add_tail(&new_node->list, &oom_kill_list);
	spin_unlock(&memhealth_lock);
}

static void mark_victim_probe(void *data, pid_t pid)
{
	unsigned long timestamp_ms = get_jiffies_64() * 1000 / HZ;
	add_oom_kill_to_list(pid, timestamp_ms);
	wake_up_interruptible(&memhealth_wq);
}

/* OOM Kill Events */
static struct proc_dir_entry *proc_mem_health_dir;
static struct oom_kill_data *last_oom_kill_data_poll;

static ssize_t oom_kill_list_read(struct file *file, char __user *buf,
			  size_t count, loff_t *offset)
{
	int bytes_read = 0, bufferSize;
	char *kernelBuffer;
	struct oom_kill_data *entry;
	loff_t index = 0, threshold = *offset;

	kernelBuffer = kmalloc(count, GFP_KERNEL);
	if (!kernelBuffer) {
		printk(KERN_ERR "memhealth failed to allocate memory for oom_kill buffer message\n");
		return bytes_read;
	}

	// Iterate through the oom list until we get to the last one read
	list_for_each_entry(entry, &oom_kill_list, list) {
		// We want to start copying once we get to the last node we saw
		if (index >= threshold) {
			char nextMsgLine[100];
			int temp_size = snprintf(nextMsgLine, 100,
				"%d %lu %u %s\n",
				entry->pid, entry->timestamp_ms, entry->uid, entry->process_name
			);

			// Check if inserting the next line causes a buffer overflow
			bufferSize = temp_size + bytes_read;
			if (bufferSize >= count) {
				break;
			}
			// Save the `nextMsgLine` into the `msg` buffer
			bytes_read += snprintf(kernelBuffer + bytes_read, count - bytes_read, nextMsgLine);
		}
		index++;
	}

	// Update the offset if we read after the last checkpoint
	if (index > threshold) {
		*offset = index;
	}

	if (copy_to_user(buf, kernelBuffer, count)) {
		printk(KERN_ERR "memhealth failed to copy oom_kill to userspace buffer\n");
		kfree(kernelBuffer);
		return -EFAULT;
	}

	kfree(kernelBuffer);
	return bytes_read;
}

static __poll_t oom_kill_list_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = DEFAULT_POLLMASK;
	struct oom_kill_data *last;

	poll_wait(filp, &memhealth_wq, wait);

	if (list_empty(&oom_kill_list)) {
		return mask;
	}

	last = list_last_entry(&oom_kill_list, struct oom_kill_data, list);
	if (last_oom_kill_data_poll != last) {
		last_oom_kill_data_poll = last;
		mask |= EPOLLPRI;
	}

	return mask;
}

static const struct proc_ops oom_kills_list_proc_ops = {
	.proc_read	= oom_kill_list_read,
	.proc_poll = oom_kill_list_poll,
};

static int __init memhealthmod_start(void)
{
	struct proc_dir_entry *entry;

	pr_info(KERN_INFO "Loading Android memhealth module...\n");

	proc_mem_health_dir = proc_mkdir(MEMHEALTH_DIRECTORY, NULL);
	if (!proc_mem_health_dir) {
		printk(KERN_ERR "memhealth failed to create directory (%s)\n", MEMHEALTH_DIRECTORY);
		return -ENOMEM;
	}

	entry = proc_create(OOM_KILL_LIST_ENTRY, 0, proc_mem_health_dir,
				&oom_kills_list_proc_ops);
	if (!entry) {
		printk(KERN_ERR "memhealth failed to create proc entry: %s\n", OOM_KILL_LIST_ENTRY);
		remove_proc_entry(MEMHEALTH_DIRECTORY, NULL);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&oom_kill_list);
	init_waitqueue_head(&memhealth_wq);
	last_oom_kill_data_poll = NULL;

	if (!register_trace_mark_victim(mark_victim_probe, NULL))
		pr_info("memhealth hooked a probe to the mark_victim tracepoint\n");
	else
		pr_warn("memhealth failed to hook a probe to the mark_victim tracepoint\n");

	pr_info(KERN_INFO "Android memhealth module loaded\n");

	return 0;
}

static void __exit memhealthmod_end(void)
{
	struct oom_kill_data *entry, *tmp;

	pr_info(KERN_INFO "Unloading Android memhealth module\n");

	if (unregister_trace_mark_victim(mark_victim_probe, NULL))
		pr_warn("memhealth failed to unhook a probe from the mark_victim tracepoint\n");

	// Traverse the linked list and free each entry
	list_for_each_entry_safe(entry, tmp, &oom_kill_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	remove_proc_entry(OOM_KILL_LIST_ENTRY, proc_mem_health_dir);
	remove_proc_entry(MEMHEALTH_DIRECTORY, NULL);
}

module_init(memhealthmod_start);
module_exit(memhealthmod_end);

MODULE_LICENSE("GPL");
