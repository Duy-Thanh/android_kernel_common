#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/oom.h>
#include <linux/tracepoint.h>
#include <trace/events/oom.h>
#include <linux/gfp.h>
#include <linux/time.h>
#include <linux/jiffies.h>

#include <asm/uaccess.h>

#define MSG_BUFFER_LEN 256	// Max size of the message buffer

static wait_queue_head_t atest_wq;
static DEFINE_SPINLOCK(atest_lock);
static uint32_t atest_level = 0;
static uint32_t atest_threshold = 0;
static int atest_event = 0;
static struct list_head oom_kill_list; // Head of the LL to store the OOM kills

struct oom_kill_data {
	pid_t pid;
	struct list_head list;
};

// Add a new OOM kill to the list
static void add_oom_kill(pid_t pid)
{
    struct oom_kill_data *new_node;//, *entry;

    // Allocate memory for our new oom_kill node
    new_node = kmalloc(sizeof(*new_node), GFP_KERNEL);
    if (!new_node) {
        printk(KERN_ERR "Failed to allocate memory for oom_kill new node\n");
        return;
    }

    // Set the fields of our new node
    new_node->pid = pid;

    // Add the new node to the list
    list_add_tail(&new_node->list, &oom_kill_list);
}

static void param_set_attr(uint32_t level, uint32_t threshold)
{
	bool trigger;

	spin_lock(&atest_lock);
	atest_level = level;
	atest_threshold = threshold;
	trigger = level > threshold && cmpxchg(&atest_event, 0, 1) == 0;
	spin_unlock(&atest_lock);

	if (trigger) {
		printk("Triggering Android test event\n");

		/**
		* Generate OOM kill to verify that we are calling mark_victim_probe()
		* out_of_memory() -> Required to temporarily export it from `mm/oom_kill.c`
		*/
		out_of_memory(&(struct oom_control) {
			.zonelist = node_zonelist(first_memory_node, GFP_KERNEL),
			.nodemask = NULL,
			.memcg = NULL,
			.gfp_mask = GFP_KERNEL,
			.order = -1,
		});
	}
}

static int param_set_level(const char *s, const struct kernel_param *kp)
{
	uint32_t val;
	int ret;

	if (!s)
		return 0;

	printk("New level received: %s\n", s);

	ret = kstrtouint(s, 10, &val);
	if (ret)
		return ret;

	if (val == atest_level)
		return 0;

	printk("New level %u accepted\n", val);

	param_set_attr(val, atest_threshold);

	return 0;
}

static const struct kernel_param_ops param_ops_level = {
	.set = param_set_level,
	.get = param_get_uint,
};

module_param_cb(level, &param_ops_level, &atest_level, 0644);

static int param_set_threshold(const char *s, const struct kernel_param *kp)
{
	uint32_t val;
	int ret;

	if (!s)
		return 0;

	printk("New threshold received: %s\n", s);

	ret = kstrtouint(s, 10, &val);
	if (ret)
		return ret;

	if (val == atest_threshold)
		return 0;

	printk("New threshold %u accepted\n", val);

	param_set_attr(atest_level, val);

	return 0;
}

static const struct kernel_param_ops param_ops_threshold = {
	.set = param_set_threshold,
	.get = param_get_uint,
};

module_param_cb(threshold, &param_ops_threshold, &atest_threshold, 0644);

static __poll_t atest_poll(struct file *file, poll_table *wait)
{
	__poll_t res = DEFAULT_POLLMASK;

	poll_wait(file, &atest_wq, wait);
	if (cmpxchg(&atest_event, 1, 0) == 1)
		res |= EPOLLPRI;

	return res;
}

static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t *offset)
{
    int bytes_read = 0, index = 0;
    struct oom_kill_data *entry;
	char msg[MSG_BUFFER_LEN];
    char *msgPtr = msg;
	loff_t startPoint = *offset;	// Index to start copying nodes from this point forward
	printk(KERN_INFO "Starting read with idx %lld\n", startPoint);

    // Traverse the linked list to the specified offset
	// Q: Should we add a lock in the case new nodes are being added while we traverse?
    list_for_each_entry(entry, &oom_kill_list, list) {
		// We want to copy any nodes from the `startPoint` forward
		if (startPoint >= index) {
			char nextMsgLine[50];
			int temp_size = snprintf(nextMsgLine, 50, "PID: %d,", entry->pid);
			// Check if inserting the next line causes a buffer overflow
			if (temp_size + bytes_read >= length) {
				printk(KERN_INFO "Exiting oom_kill looping gracefully...");
				break;
			}

			// Save the `nextMsgLine` into the `msg` buffer
			bytes_read += snprintf(msgPtr + bytes_read, MSG_BUFFER_LEN - bytes_read, nextMsgLine);
			printk(KERN_INFO "Adding the following line to buffer:%s\n", nextMsgLine);
		}
		index++;
    }

	*offset = index;	// Update the offset to last index seen/copied

    if (copy_to_user(buffer, msgPtr, bytes_read) != 0) {
		printk(KERN_ERR "Failed to copy data to userspace buffer\n");
        return -EFAULT;
    }

    return bytes_read;
}

static int device_open(struct inode *inode, struct file *file)
{
    INIT_LIST_HEAD(&oom_kill_list);	// Initialize the linked list
	return 0;
}


static const struct file_operations atest_fops = {
	.owner = THIS_MODULE,
	.poll = atest_poll,
	.read = device_read,
	.open = device_open,
};

static struct miscdevice atest_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "atestmod",
	.mode		= 0666,
	.fops		= &atest_fops,
};

static void mark_victim_probe(void *data, pid_t pid)
{
	spin_lock(&atest_lock);
	add_oom_kill(pid);
	spin_unlock(&atest_lock);
	wake_up_interruptible(&atest_wq);
}

static int __init atestmod_start(void)
{
	int result;

	printk(KERN_INFO "Loading Android test module...\n");
	result = misc_register(&atest_misc);
	if (result < 0)
		return -ENODEV;

	init_waitqueue_head(&atest_wq);
	if (!register_trace_mark_victim(mark_victim_probe, NULL))
		printk(KERN_INFO "Hooked a probe to the mark_victim tracepoint\n");
	else
		printk(KERN_WARNING "Failed to hook a probe to the mark_victim tracepoint\n");

	printk(KERN_INFO "Android test module loaded\n");

	return 0;
}

static void __exit atestmod_end(void)
{
	struct oom_kill_data *entry, *tmp;

	printk(KERN_INFO "Unloading Android test module\n");
	if (unregister_trace_mark_victim(mark_victim_probe, NULL))
		printk(KERN_WARNING "Failed to unhook a probe from the mark_victim tracepoint\n");


	// Traverse the linked list and free each entry
    list_for_each_entry_safe(entry, tmp, &oom_kill_list, list) {
        list_del(&entry->list);
        kfree(entry);
    }

	misc_deregister(&atest_misc);
}

module_init(atestmod_start);
module_exit(atestmod_end);
MODULE_LICENSE("GPL");