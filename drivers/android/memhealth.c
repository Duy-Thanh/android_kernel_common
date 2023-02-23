#include <linux/cred.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <trace/events/oom.h>

#define MSG_BUFFER_LEN 4096	// 4k bytes max size for kernel message buffer

static DEFINE_SPINLOCK(memhealth_lock);
static struct list_head oom_kill_list; // Head of the LL to store the OOM kills data

/* The following attribute is for testing only! */
static uint32_t test_event;

struct oom_kill_data {
	pid_t pid;
	uid_t uid;
	char process_name[TASK_COMM_LEN];
	unsigned long timestamp_ms;
	struct list_head list;
};

// Add a new OOM kill to the list
static void add_oom_kill_to_list(pid_t pid, unsigned long timestamp)
{
	struct oom_kill_data *new_node;
	struct task_struct *task;
	const struct cred *cred;
	struct pid *pid_struct;

	// Allocate memory for our new oom_kill node
	new_node = kmalloc(sizeof(*new_node), GFP_KERNEL);
	if (!new_node) {
		printk(KERN_ERR "Failed to allocate memory for oom_kill new node\n");
		return;
	}

	// Look up task associated with `pid`
	pid_struct = find_get_pid(pid);
	rcu_read_lock();
	task = pid_task(pid_struct, PIDTYPE_PID);
	rcu_read_unlock();
	if (task == NULL) {
		printk(KERN_ERR "Process with pid %d not found\n", pid);
		return;
	}

	// `get_task_comm` requests the lock, but we already have the lock from `__oom_kill_process`
	task_unlock(task);	// wait to free lock
	// Set the process name into our new node
	if (get_task_comm(new_node->process_name, task) <= 0) {
		printk(KERN_ERR "Error getting task command");
		return;
	}
	cred = get_task_cred(task);
	if (cred == NULL) {
		printk(KERN_ERR "Error getting cred from task");
		return;
	}

	// Set the fields of our new node
	new_node->pid = pid;
	new_node->timestamp_ms = timestamp;
	new_node->uid = cred->uid.val;

	printk(KERN_INFO "Inserting node with (pid, %d), (uid, %u), (name, %s)\n", new_node->pid, new_node->uid, new_node->process_name);

    // Add the new node to the list, at the head
	spin_lock(&memhealth_lock);
	list_add(&new_node->list, &oom_kill_list);
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
		for (int i = 0; i < 3; i++) {	// Generate some oom_kills
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
static int oom_count;

static struct miscdevice memhealth_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "memhealthmod",
};

static ssize_t oom_count_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oom_count);
}

static struct kobj_attribute dev_attr_oom_count = __ATTR_RO(oom_count);

static ssize_t oom_kill_list_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	int bytes_read = 0;
	char *kernelBuffer;
	bool unreachableFlag = false;
	struct oom_kill_data *entry;

	kernelBuffer = kmalloc(MSG_BUFFER_LEN, GFP_KERNEL);
	if (!kernelBuffer) {
		printk(KERN_ERR "Failed to allocate memory for kernel buffer message\n");
        return -EFAULT;
	}

	list_for_each_entry(entry, &oom_kill_list, list) {
		char nextMsgLine[100];
		int temp_size = snprintf(nextMsgLine, 100, "PID:%d,timestamp:%lu,UID:%u,process name:%s\n", entry->pid, entry->timestamp_ms, entry->uid, entry->process_name);

		// Check if inserting the next line causes a buffer overflow
		if (temp_size + bytes_read >= MSG_BUFFER_LEN) {
			printk(KERN_INFO "Exiting oom_kill looping gracefully...");
			unreachableFlag = true;
			break;
		}
		// Save the `nextMsgLine` into the `msg` buffer
		bytes_read += snprintf(kernelBuffer + bytes_read, MSG_BUFFER_LEN - bytes_read, nextMsgLine);
		printk(KERN_INFO "Adding the following line to buffer:%s\n", nextMsgLine);
	}

	if (unreachableFlag) {
		// We get here if we reached our 4k buffer limit. We have to remove all
		// nodes from the last `entry` point forward, since they are now unreachable.
		// They are unreachable since newer nodes are being inserted at the head,
		// and the 4k buffer limit makes it impossible to reach our oldest nodes.
		spin_lock(&memhealth_lock);
		// Iterate from the last node we saw before breaking
		list_for_each_entry_from(entry, &oom_kill_list, list) {
			list_del(&entry->list);
			kfree(entry);
		}
		spin_unlock(&memhealth_lock);
	}

	// Copy the data from the kernel buffer to the userspace buffer
	if (sprintf(buf, "%s", kernelBuffer) < 0) {
		printk(KERN_ERR "Failed to copy oom_kill to userspace buffer\n");
		return -EFAULT;
	}

	kfree(kernelBuffer);
	return bytes_read;
}

static struct kobj_attribute dev_attr_oom_kill_list = __ATTR_RO(oom_kill_list);

static struct attribute *memhealth_attributes[] = {
	&dev_attr_oom_kill_list.attr,
	&dev_attr_oom_count.attr,
	NULL
};

static struct attribute_group memhealth_attr_group = {
	.name = "memhealth",
	.attrs = memhealth_attributes,
};

static void mark_victim_probe(void *data, pid_t pid)
{
	unsigned long timestamp_ms = get_jiffies_64() * 1000 / HZ;	// Save current time in milliseconds
	pr_info("OOM-killer killed process %d, timestamp(ms) %lu\n", pid, timestamp_ms);
	add_oom_kill_to_list(pid, timestamp_ms);
	spin_lock(&memhealth_lock);
	oom_count++;
	spin_unlock(&memhealth_lock);
	sysfs_notify(mm_kobj, memhealth_attr_group.name, dev_attr_oom_count.attr.name);
}

static int __init memhealthmod_start(void)
{
	int result;

	pr_info(KERN_INFO "Loading Android test module...\n");
	result = misc_register(&memhealth_misc);
	if (result < 0)
		return -ENODEV;

	if (sysfs_create_group(mm_kobj, &memhealth_attr_group))
		pr_err("memhealth: failed to create sysfs group\n");

	if (!register_trace_mark_victim(mark_victim_probe, NULL))
		pr_info("Hooked a probe to the mark_victim tracepoint\n");
	else
		pr_warn("Failed to hook a probe to the mark_victim tracepoint\n");

	INIT_LIST_HEAD(&oom_kill_list);

	pr_info(KERN_INFO "Android test module loaded\n");

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

	sysfs_remove_group(mm_kobj, &memhealth_attr_group);

	misc_deregister(&memhealth_misc);
}

module_init(memhealthmod_start);
module_exit(memhealthmod_end);

MODULE_LICENSE("GPL");
