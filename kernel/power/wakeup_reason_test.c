#include <linux/kernel.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/wakeup_reason.h>
#include <linux/delay.h>
#include <linux/kernel_stat.h>
#include <linux/sched/task_stack.h>
#include <linux/completion.h>
#include <linux/random.h>

#include "power.h"

#define TEST_TS_NUM 100  //thread per test item
#define TEST_MS_SLEEP 10
#define TEST_IRQ_NUM_MAX 256
#define TEST_CLEAR_MAJIC 17
#define TEST_WS_NAME_MAX 64

#define TEST_REASON_STR_MAX 512

enum {
	TEST_CAP_IRQ = 0,
	TEST_CAP_WS,
	TEST_CLEAR,
	TEST_PRINT,
	TEST_ADD_WS,
	TEST_DEL_WS,
	//TEST_SUSPEND,

	TEST_ITEM_MAX,
};

static struct task_struct *ts[TEST_TS_NUM * TEST_ITEM_MAX];
static int thread_stop = 0;
static struct completion cp[TEST_TS_NUM * TEST_ITEM_MAX];
static char reason_str[TEST_REASON_STR_MAX + 32]; //reason string and "deadbeef"
static DEFINE_MUTEX(suspend_lock);

static int wr_test_thread(void *data)
{
	static unsigned int irq = 0;
	static char ws[TEST_WS_NAME_MAX];
	struct task_struct **ts_pos = (struct task_struct**)data;
	int id = (int)(ts_pos - &ts[0]), ret;
	unsigned int random;
	size_t size;

	while (!thread_stop && !kthread_should_stop()) {
		if (id < (TEST_TS_NUM * (TEST_CAP_IRQ + 1))) {
			/* ## TEST_CAP_IRQ: capture irq sources */
			log_irq_wakeup_reason(irq++ % TEST_IRQ_NUM_MAX);
		} else if (id < (TEST_TS_NUM * (TEST_CAP_WS + 1))) {
			/* ## TEST_CAP_WS: capture ws sources */
			log_ws_wakeup_reason();
		} else if (id < (TEST_TS_NUM * (TEST_CLEAR + 1))) {
			/* ## TEST_CLEAR: clean wakeup reason */
			if (!(id % TEST_CLEAR_MAJIC)) {
				printk(KERN_INFO "clearing wakeup_reason, id[%d]\n", id);
				clear_wakeup_reason();
				printk(KERN_INFO "clearing wakeup_reason done, id[%d]\n", id);
			}
		} else if (id < (TEST_TS_NUM * (TEST_PRINT + 1))) {
			/* ## TEST_PRINT: get wakeup reason and print */
			memset(reason_str, 0, sizeof(reason_str));

			/* assume random input string length, and check if buffer overflow */
			get_random_bytes(&random, sizeof(random));
			random %= TEST_REASON_STR_MAX;
			snprintf(reason_str + random, 8 + 1, "deadbeef");

			size = last_wakeup_reason_get(reason_str, random);
			if (size)
				printk(KERN_INFO "last wakeup reason[%d]:%s\n", random, reason_str);
			else
				printk(KERN_INFO "empty last wakeup reason\n");

			if (strncmp(reason_str + random, "deadbeef", 8)) {
				printk(KERN_ERR
					"ERROR: get last wakeup reason string overflow:[%s]\n",
					reason_str + random);
			}
		} else if (id < (TEST_TS_NUM * (TEST_ADD_WS + 1))) {
			/* ## TEST_ADD_WS: add wakeup sources */
			memset(ws, 0, sizeof(ws));
			snprintf(ws, sizeof(ws), "test_ws_id_%d", id % TEST_TS_NUM);
			//printk(KERN_INFO "add ws[%s] done\n", ws);
			ret = pm_wake_lock(ws);
			if (ret) {
				printk(KERN_ERR "pm wake lock failed\n");
			} else {
				//printk(KERN_INFO "add ws[%s] done\n", ws);
			}
		} else if (id < (TEST_TS_NUM * (TEST_DEL_WS + 1))) {
			/* ## TEST_DEL_WS: delete wakeup sources */
			memset(ws, 0, sizeof(ws));
			snprintf(ws, sizeof(ws), "test_ws_id_%d", id % TEST_TS_NUM);
			//printk(KERN_INFO "clearing ws[%s]\n", ws);
			pm_wake_unlock(ws);
			//printk(KERN_INFO "clear ws[%s] done\n", ws);
		}

		//msleep(TEST_MS_SLEEP + id);
		wait_for_completion_timeout(&cp[id],
					msecs_to_jiffies(TEST_MS_SLEEP + id));
	}

	printk(KERN_INFO "thread id[%d] end\n", id);

	while (!kthread_should_stop()) {
		msleep(1);
	}

	return 0;
}

static int __init wr_test_init(void)
{
	int i;

	for (i = 0; i < (TEST_TS_NUM * TEST_ITEM_MAX); i++) {

		init_completion(&cp[i]);

		ts[i] = kthread_create(wr_test_thread, &ts[i], "wr_test[%d]", i);
		if (IS_ERR(ts[i])) {
			pr_err("create test task[%d] failed\n", i);
			return -1;
		}
		wake_up_process(ts[i]);
	}

	return 0;
}
module_init(wr_test_init);

static void __exit wr_test_exit(void)
{
	int i, ret;

	thread_stop = 1;

	for (i = 0; i < (TEST_TS_NUM * TEST_ITEM_MAX); i++) {
		printk(KERN_INFO "stopping thread id[%d]\n", i);
		complete(&cp[i]);
		ret = kthread_stop(ts[i]);
		if (ret) {
			pr_err("stop test task[%d] failed\n", i);
			continue;
		}
		printk(KERN_INFO "stop thread id[%d], succeed.\n", i);
	}

	return;
}
module_exit(wr_test_exit);

MODULE_LICENSE("GPL v2");
