#include <linux/wait.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/statfs.h>
#include <uapi/linux/magic.h>

extern int kern_path(const char *name, unsigned int flags, struct path *path);
extern int vfs_statfs(const struct path *path, struct kstatfs *buf);
extern int blkdev_issue_flush(struct block_device *bdev);
extern void f2fs_handle_panic(struct super_block *sb);

void (*oem_panic_flush)(void);
EXPORT_SYMBOL_GPL(oem_panic_flush);

#define	ANDROID_DATA_PARTITION_PATH		"/data"

#define DEFAULT_WAIT_FLUSH_TIMEOUT		2000	/* ms */

struct panic_handle_operations {
	void (*handle_stop)(struct super_block *);
};

enum {
	F2FS_PANIC_HANDLE,
	MAX_PANIC_HANDLE,
};

#ifdef CONFIG_F2FS_FS
static const struct panic_handle_operations f2fs_panic_handle = {
	.handle_stop	= f2fs_handle_panic,
};
#endif

static const struct panic_handle_operations *default_ops[MAX_PANIC_HANDLE] = {
#ifdef CONFIG_F2FS_FS
	&f2fs_panic_handle,
#else
	NULL,
#endif
};

struct panic_flush_control {
	struct task_struct *flush_thread;
	struct wait_queue_head flush_wq;
	const struct panic_handle_operations *ops;
	bool trigger_flush;
};

static struct panic_flush_control *pfc;

static struct super_block *lookup_sueprblock(void)
{
	struct path kpath;
	struct kstatfs st;

	if (kern_path(ANDROID_DATA_PARTITION_PATH, LOOKUP_FOLLOW, &kpath))
		return NULL;
	if (vfs_statfs(&kpath, &st))
		return NULL;
	if (kpath.dentry && kpath.dentry->d_sb) {
		pr_debug("%s: found data sb.", __func__);
		return kpath.dentry->d_sb;
	}

	return NULL;
}

static void do_panic_flush(void)
{
	struct super_block *sb;
	int ret;

	sb = lookup_sueprblock();
	if (!sb) {
		pr_err("%s: get_f2fs_sb failed\n", __func__);
		return;
	}

	if (sb_rdonly(sb))
		return;

	if (oem_panic_flush)
		oem_panic_flush();

	ret = blkdev_issue_flush(sb->s_bdev);
	if (!ret)
		pr_emerg("blkdev_issue_flush before panic return success\n");

	switch (sb->s_magic) {
	case F2FS_SUPER_MAGIC:
		pfc->ops[F2FS_PANIC_HANDLE].handle_stop(sb);
		break;
	default:
		break;
	}
}

static int panic_flush_thread(void *data)
{
	static bool flushed = false;

	wait_event(pfc->flush_wq,
		kthread_should_stop() || pfc->trigger_flush);

	while (!kthread_should_stop()) {
		if (flushed)
			continue;

		do_panic_flush();
		flushed = true;
	}

	return 0;
}

static int __init create_panic_flush_control(void)
{
	int err;

	pfc = kzalloc(sizeof(*pfc), GFP_KERNEL);
	if (!pfc) {
		pr_err("%s: fail to allocate memory\n", __func__);
		return -ENOMEM;
	}

	init_waitqueue_head(&pfc->flush_wq);
	pfc->ops = default_ops[F2FS_PANIC_HANDLE];

	pfc->flush_thread = kthread_run(panic_flush_thread, pfc, "panic_flush");
	if (IS_ERR(pfc->flush_thread)) {
		err = PTR_ERR(pfc->flush_thread);
		pfc->flush_thread = NULL;
		kfree(pfc);
		pfc = NULL;
		return err;
	}

	return 0;
}

void do_handle_panic(void)
{
	unsigned long last_time = jiffies + DEFAULT_WAIT_FLUSH_TIMEOUT * HZ;

	pfc->trigger_flush = true;
	smp_mb();

	if (waitqueue_active(&pfc->flush_wq))
		wake_up(&pfc->flush_wq);

repeat:
	if (!time_after(jiffies, last_time))
		goto repeat;
}
EXPORT_SYMBOL(do_handle_panic);

static void __exit destroy_panic_flush_control(void)
{
	if (pfc && pfc->flush_thread) {
		pr_debug("%s: stop panic_flush thread\n", __func__);
		kthread_stop(pfc->flush_thread);
		kfree(pfc);
		pfc = NULL;
	}
}

module_init(create_panic_flush_control);
module_exit(destroy_panic_flush_control);
MODULE_DESCRIPTION("panic flush control");
MODULE_LICENSE("GPL v2");
