// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>

#include "../mmu_notifier_ppps.h"

#include "../../ppps/ppps_misc_module.h"

struct mmu_notifier_ppps_ctx {
	struct mmu_notifier notifier;
	/* Serializes updates to the observed range and callback statistics. */
	spinlock_t lock;
	struct mmu_notifier_ppps_range range;
	struct mmu_notifier_ppps_stats stats;
};

static void mmu_notifier_ppps_record(struct mmu_notifier *notifier,
				     unsigned long start,
				     unsigned long end)
{
	struct mmu_notifier_ppps_ctx *ctx = container_of(notifier,
		struct mmu_notifier_ppps_ctx, notifier);
	unsigned long flags;
	u64 span;

	spin_lock_irqsave(&ctx->lock, flags);
	ctx->stats.total_callbacks++;
	if (start < ctx->range.start || start >= ctx->range.end)
		goto unlock;

	span = end - start;
	ctx->stats.callbacks++;
	ctx->stats.last_start = start;
	ctx->stats.last_end = end;
	ctx->stats.max_span = max(ctx->stats.max_span, span);
	/*
	 * Ranges are in whole process pages.  Per-PTE callbacks span one
	 * process page; folio-granular ones (a packed anonymous tuple) span
	 * at most one native page.
	 */
	if (span % ctx->range.page_size || span > PAGE_SIZE ||
	    start % ctx->range.page_size)
		ctx->stats.bad_ranges++;

unlock:
	spin_unlock_irqrestore(&ctx->lock, flags);
}

static int mmu_notifier_ppps_clear_young(struct mmu_notifier *notifier,
					 struct mm_struct *mm,
					 unsigned long start,
					 unsigned long end)
{
	mmu_notifier_ppps_record(notifier, start, end);
	return false;
}

static int
mmu_notifier_ppps_clear_flush_young(struct mmu_notifier *notifier,
				    struct mm_struct *mm,
				    unsigned long start,
				    unsigned long end)
{
	mmu_notifier_ppps_record(notifier, start, end);
	return false;
}

static const struct mmu_notifier_ops mmu_notifier_ppps_ops = {
	.clear_young = mmu_notifier_ppps_clear_young,
	.clear_flush_young = mmu_notifier_ppps_clear_flush_young,
};

static int mmu_notifier_ppps_open(struct inode *inode, struct file *file)
{
	struct mmu_notifier_ppps_ctx *ctx;
	int ret;

	if (!current->mm)
		return -EINVAL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	spin_lock_init(&ctx->lock);
	ctx->notifier.ops = &mmu_notifier_ppps_ops;
	ret = mmu_notifier_register(&ctx->notifier, current->mm);
	if (ret) {
		kfree(ctx);
		return ret;
	}

	file->private_data = ctx;
	return 0;
}

static int mmu_notifier_ppps_release(struct inode *inode, struct file *file)
{
	struct mmu_notifier_ppps_ctx *ctx = file->private_data;

	mmu_notifier_unregister(&ctx->notifier, ctx->notifier.mm);
	kfree(ctx);
	return 0;
}

static long mmu_notifier_ppps_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	struct mmu_notifier_ppps_ctx *ctx = file->private_data;
	void __user *argp = (void __user *)arg;
	unsigned long flags;

	switch (cmd) {
	case MMU_NOTIFIER_PPPS_SET_RANGE: {
		struct mmu_notifier_ppps_range range;

		if (copy_from_user(&range, argp, sizeof(range)))
			return -EFAULT;
		if (!range.page_size || range.start >= range.end)
			return -EINVAL;

		spin_lock_irqsave(&ctx->lock, flags);
		ctx->range = range;
		memset(&ctx->stats, 0, sizeof(ctx->stats));
		spin_unlock_irqrestore(&ctx->lock, flags);
		return 0;
	}
	case MMU_NOTIFIER_PPPS_GET_STATS: {
		struct mmu_notifier_ppps_stats stats;

		spin_lock_irqsave(&ctx->lock, flags);
		stats = ctx->stats;
		spin_unlock_irqrestore(&ctx->lock, flags);
		if (copy_to_user(argp, &stats, sizeof(stats)))
			return -EFAULT;
		return 0;
	}
	default:
		return -ENOTTY;
	}
}

static const struct file_operations mmu_notifier_ppps_fops = {
	.owner = THIS_MODULE,
	.open = mmu_notifier_ppps_open,
	.release = mmu_notifier_ppps_release,
	.unlocked_ioctl = mmu_notifier_ppps_ioctl,
};

PPPS_MISC_MODULE("mmu_notifier_ppps", &mmu_notifier_ppps_fops, 0600,
		 ppps_misc_no_setup, ppps_misc_no_teardown,
		 "PPPS MMU notifier range regression test helper");
