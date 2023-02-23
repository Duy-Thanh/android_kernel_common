// SPDX-License-Identifier: GPL-2.0

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
static struct list_head oom_kill_list; // Head of the LL to store the OOM kills data
static wait_queue_head_t memhealth_wq;

/* The following attribute is for testing only! */
static uint32_t test_event;

struct oom_kill_data {
	pid_t pid;
	uid_t uid;
	char process_name[TASK_COMM_LEN];
	unsigned long timestamp_ms;
	struct list_head list;
};

/* Add a new OOM kill to the list */
static void add_oom_kill_to_list(pid_t pid, unsigned long timestamp)
{
	struct oom_kill_data *new_node;
	struct task_struct *task;
	const struct cred *cred;
	struct pid *pid_struct;

	// Allocate memory for our new oom_kill node using `GFP_ATOMIC`.
	// Our function occurs during a `task_lock`, and we want to make
	// sure that our memory allocation will not be interrupted
	new_node = kmalloc(sizeof(*new_node), GFP_ATOMIC);
	if (!new_node) {
		printk(KERN_ERR "Failed to allocate memory for oom_kill new node\n");
		return;
	}

	// Look up task associated with `pid`
	pid_struct = find_get_pid(pid);
	if (pid_struct == NULL) {
		printk(KERN_ERR "Failed to get pid %d\n", pid);
		return;
	}

	task = get_pid_task(pid_struct, PIDTYPE_PID);
	if (task == NULL) {
		printk(KERN_ERR "Process with pid %d not found\n", pid);
		return;
	}

	put_pid(pid_struct);

	cred = get_task_cred(task);
	if (cred == NULL) {
		printk(KERN_ERR "Error getting cred from task\n");
		return;
	}

	// Set the process name in the new node
	if (strscpy_pad(new_node->process_name, task->comm, TASK_COMM_LEN) < 0) {
		printk(KERN_ERR "Failed to copy process name to new node\n");
		return;
	}

	put_task_struct(task);

	// Set the other fields of our new node
	new_node->pid = pid;
	new_node->timestamp_ms = timestamp;
	new_node->uid = cred->uid.val;

	put_cred(cred);

    // Add the new node to the list
	spin_lock(&memhealth_lock);
	list_add_tail(&new_node->list, &oom_kill_list);
	spin_unlock(&memhealth_lock);
}

static int param_set_test_event(const char *s, const struct kernel_param *kp)
{
	uint32_t val;
	int ret;

	if (!s)
		return 0;

	ret = kstrtouint(s, 10, &val);
	if (ret)
		return ret;

	switch (val)
	{
	case 1:
		pr_info("Generating OOM-kill\n");

		/**
		* Generate OOM kills to verify that we are calling mark_victim_probe()
		* out_of_memory() -> Required to temporarily export it from `mm/oom_kill.c`
		*/
		for (int i = 0; i < 2; i++) {	// Generate some oom_kills
			out_of_memory(&(struct oom_control) {
				.zonelist = node_zonelist(first_memory_node, GFP_KERNEL),
				.nodemask = NULL,
				.memcg = NULL,
				.gfp_mask = GFP_KERNEL,
				.order = -1,
			});
		}
		test_event = val;
		break;
	default:
		pr_err("Unknown event %d\n", val);
	}

	return 0;
}

static const struct kernel_param_ops param_ops_test_event = {
	.set = param_set_test_event,
	.get = param_get_uint,
};

module_param_cb(test_event, &param_ops_test_event, &test_event, 0644);

/* Memhealth module */

static struct miscdevice memhealth_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "memhealthmod",
};

static void mark_victim_probe(void *data, pid_t pid)
{
	unsigned long timestamp_ms = get_jiffies_64() * 1000 / HZ;	// Save current time in milliseconds
	pr_info("OOM-killer killed process %d, timestamp(ms) %lu\n", pid, timestamp_ms);
	add_oom_kill_to_list(pid, timestamp_ms);
	wake_up_interruptible(&memhealth_wq);
}


// Procfs setup for `oom_kill_list`
static struct proc_dir_entry *proc_mem_health_dir;
static struct oom_kill_data *last_oom_kill_data_poll;

static ssize_t oom_kill_list_read(struct file *file, char __user *buf,
			  size_t count, loff_t *offset)
{
	int bytes_read = 0, bufferSize;
	char *kernelBuffer;
	struct oom_kill_data *entry;
	loff_t index = 0, threshold = *offset;
	printk(KERN_INFO "Attempting to read oom_kill_list, initial offset: %lld\n", threshold);

	kernelBuffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!kernelBuffer) {
		printk(KERN_ERR "Failed to allocate memory for kernel buffer message\n");
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
			printk(KERN_INFO "New line size: %d\n", temp_size);

			// Check if inserting the next line causes a buffer overflow
			bufferSize = temp_size + bytes_read;
			printk(KERN_INFO "New total buffer size: %d\n", bufferSize);
			if (bufferSize >= PAGE_SIZE) {
				printk(KERN_INFO "Exiting oom_kill looping gracefully...");
				break;
			}
			// Save the `nextMsgLine` into the `msg` buffer
			bytes_read += snprintf(kernelBuffer + bytes_read, PAGE_SIZE - bytes_read, nextMsgLine);
			printk(KERN_INFO "Adding the following line to buffer:%s\n", nextMsgLine);
		}
		index++;
	}

	// Remove the last character inserted (`\n`) from the message
	if (bytes_read > 0) {
		kernelBuffer[bytes_read - 1] = '\0';
		bytes_read--;
	}

	// Update the offset if we read after the last checkpoint
	if (index > threshold) {
		*offset = index;
		printk(KERN_INFO "Updated offset from %lld to %lld\n", threshold, index);
	}

	// Copy the data from the kernel buffer to the userspace buffer
	if (copy_to_user(buf, kernelBuffer, PAGE_SIZE)) {
		printk(KERN_ERR "Failed to copy oom_kill to userspace buffer\n");
		kfree(kernelBuffer);
		return -EFAULT;
	}

	printk(KERN_INFO "Success in reading oom_kill_list\n");
	kfree(kernelBuffer);
	return bytes_read;
}

static __poll_t oom_kill_list_poll(struct file *filp, poll_table *wait)
{
	__poll_t mask = DEFAULT_POLLMASK;
	struct oom_kill_data *last;

	printk(KERN_INFO "Starting to poll on oom_kill_list...\n");

	poll_wait(filp, &memhealth_wq, wait);

	if (list_empty(&oom_kill_list)) {
		return mask;
	}

	// Get the current last event in our oom_kill_list
	last = list_last_entry(&oom_kill_list, struct oom_kill_data, list);
	if (last_oom_kill_data_poll != last) {
		printk(KERN_INFO "Updating last_oom_kill_data_poll\n");
		last_oom_kill_data_poll = last;
		mask |= EPOLLPRI;
	}

	printk(KERN_INFO "Finished to poll on oom_kill_list\n");
	return mask;
}

static const struct proc_ops oom_kills_list_proc_ops = {
	.proc_read	= oom_kill_list_read,
	.proc_poll = oom_kill_list_poll,
};

static int __init memhealthmod_start(void)
{
	struct proc_dir_entry *entry;
	int result;

	pr_info(KERN_INFO "Loading Android test module...\n");
	result = misc_register(&memhealth_misc);
	if (result < 0) {
		printk(KERN_ERR "Failed to register device\n");
		return -ENODEV;
	}

	proc_mem_health_dir = proc_mkdir(MEMHEALTH_DIRECTORY, NULL);
	if (!proc_mem_health_dir) {
		printk(KERN_ERR "Error creating directory (%s)\n", MEMHEALTH_DIRECTORY);
		return -ENOMEM;
	}

	entry = proc_create(OOM_KILL_LIST_ENTRY, 0, proc_mem_health_dir,
				&oom_kills_list_proc_ops);
	if (!entry) {
		printk(KERN_ERR "Failed to create proc for %s\n", OOM_KILL_LIST_ENTRY);
		remove_proc_entry(MEMHEALTH_DIRECTORY, NULL);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&oom_kill_list);
	init_waitqueue_head(&memhealth_wq);
	last_oom_kill_data_poll = NULL;

	if (!register_trace_mark_victim(mark_victim_probe, NULL))
		pr_info("Hooked a probe to the mark_victim tracepoint\n");
	else
		pr_warn("Failed to hook a probe to the mark_victim tracepoint\n");

	pr_info(KERN_INFO "Android memhealth module loaded\n");

	return 0;
}

static void __exit memhealthmod_end(void)
{
	struct oom_kill_data *entry, *tmp;

	pr_info(KERN_INFO "Unloading Android test module\n");

	if (unregister_trace_mark_victim(mark_victim_probe, NULL))
		pr_warn("Failed to unhook a probe from the mark_victim tracepoint\n");

	// Traverse the linked list and free each entry
	list_for_each_entry_safe(entry, tmp, &oom_kill_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	remove_proc_entry(OOM_KILL_LIST_ENTRY, proc_mem_health_dir);
	remove_proc_entry(MEMHEALTH_DIRECTORY, NULL);

	misc_deregister(&memhealth_misc);
}

module_init(memhealthmod_start);
module_exit(memhealthmod_end);

MODULE_LICENSE("GPL");
