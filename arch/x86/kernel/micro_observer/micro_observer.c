#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

/* External reference to your existing function */
extern void wlc_exec(void);
extern int wlc_init(void);

#define OBSERVER_PERIOD_MS 20

static struct delayed_work observer_dwork;

static void observer_work_handler(struct work_struct *work)
{
    wlc_exec();

    schedule_delayed_work(&observer_dwork, msecs_to_jiffies(OBSERVER_PERIOD_MS));
}

static int __init micro_observer_init(void)
{
    pr_info("Micro Observer: Initializing periodic execution (20ms)\n");

    wlc_init();

    INIT_DELAYED_WORK(&observer_dwork, observer_work_handler);
    
    schedule_delayed_work(&observer_dwork, msecs_to_jiffies(OBSERVER_PERIOD_MS));

    return 0;
}

static void __exit micro_observer_exit(void)
{
    pr_info("Micro Observer: Stopping workqueue and exiting\n");
    cancel_delayed_work_sync(&observer_dwork);
}

module_init(micro_observer_init);
module_exit(micro_observer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Charles Yang");
MODULE_DESCRIPTION("Periodic trigger for GPA Micro Observer");

