// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Google, Inc.
 */
#include <linux/compiler.h>
#include <linux/platform_device.h>
#include <linux/trusty/smcall.h>
#include <linux/trusty/trusty.h>
#include <linux/notifier.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/log2.h>
#include <asm/page.h>
#include "trusty-log.h"
#include "trusty-logbuffer.h"

#define TRUSTY_LOG_SIZE (PAGE_SIZE * 2)
#define TRUSTY_LINE_BUFFER_SIZE 256

/*
 * If we log too much and a UART or other slow source is connected, we can stall
 * out another thread which is doing printk.
 *
 * Trusty crash logs are currently ~16 lines, so 100 should include context and
 * the crash most of the time.
 */
static struct ratelimit_state trusty_log_rate_limit =
	RATELIMIT_STATE_INIT("trusty_log", 1 * HZ, 100);

struct trusty_log_sink_state {
	struct seq_file *sfile;
	/*
	 * This lock is here to ensure only one consumer will read
	 * from the log ring buffer at a time.
	 */
	spinlock_t lock;
	u32 start; /* sink unwrapped index when start offset is zero */
	u32 get; /* sink current get unwrapped index */
	u32 last_successful_next; /* track the last successful get in order to retry */
};

struct trusty_log_state {
	struct device *dev;
	struct device *trusty_dev;
	struct trusty_logbuffer logbuffer;
	struct log_rb *log;
	struct page *log_pages;
	struct scatterlist sg;
	trusty_shared_mem_id_t log_pages_shared_mem_id;
	struct trusty_log_sink_state klog_sink;
	struct trusty_log_sink_state logbuffer_sink;

	struct notifier_block call_notifier;
	struct notifier_block panic_notifier;
	char line_buffer[TRUSTY_LINE_BUFFER_SIZE];
};

static int log_read_line(struct trusty_log_state *s, int put, int get)
{
	struct log_rb *log = s->log;
	int i;
	char c = '\0';
	size_t max_to_read =
		min_t(size_t, put - get, sizeof(s->line_buffer) - 1);
	size_t mask = log->sz - 1;

	for (i = 0; i < max_to_read && c != '\n';)
		s->line_buffer[i++] = c = log->data[get++ & mask];
	s->line_buffer[i] = '\0';

	return i;
}

/**
 * trusty_log_start() - initialise the sink iteration either to kernel log
 * or to secondary logbuffer
 * @s:         Current log state.
 * @sink:      trusty_log_sink_state holding the get index on a given sink
 * @index:     Unwrapped ring buffer index from where iteration shall start
 *
 * Return: the start index pointer
 */
static void *trusty_log_start(struct trusty_log_state *s,
			      struct trusty_log_sink_state *sink, u32 index)
{
	struct log_rb *log;
	BUG_ON(!s);
	log = s->log;
	if (WARN_ON(!is_power_of_2(log->sz))) {
		return ERR_PTR(-EINVAL);
	}
	sink->get = index;
	if (sink->get >= log->alloc) {
		return NULL;
	}
	if (sink->get == log->put) {
		return NULL;
	}
	return (&sink->get);
}

/**
 * trusty_log_next() - iteration next function
 * or to secondary logbuffer
 * @s:         Current log state.
 * @sink:      trusty_log_sink_state holding the get index on a given sink
 *
 * Return: the next index pointer or ENOMEM if iteration is complete
 */
static inline void *trusty_log_next(struct trusty_log_state *s,
				    struct trusty_log_sink_state *sink)
{
	struct log_rb *log = s->log;
	if (sink->get == log->put) {
		return NULL;
	}
	return &sink->get;
}

/**
 * trusty_log_show() - sink log entry at current iteration
 * @s:         Current log state.
 * @sink:      trusty_log_sink_state holding the get index on a given sink
 *
 * Return: 0 is suceesful, negative error code otherwise
 */
static int trusty_log_show(struct trusty_log_state *s,
			   struct trusty_log_sink_state *sink)
{
	struct log_rb *log = s->log;
	u32 put, alloc, get;
	bool oldest_line;
	int read_chars;
	bool trusty_panicked = trusty_get_panic_status(s->trusty_dev);
	/*
	 * For this ring buffer, at any given point, alloc >= put >= get.
	 * The producer side of the buffer is not locked, so the put and alloc
	 * pointers must be read in a defined order (put before alloc) so
	 * that the above condition is maintained. A read barrier is needed
	 * to make sure the hardware and compiler keep the reads ordered.
	 */
	get = sink->get;
	put = log->put;
	alloc = log->alloc;
	if (put == get) {
		return 0;
	}
	oldest_line = (get == alloc - log->sz) ? true : false;
	/* Make sure that the read of put occurs before the read of log data */
	rmb();

	/* Read a line from the log */
	read_chars = log_read_line(s, put, get);
	sink->get += read_chars;
	/* Force the loads from log_read_line to complete. */
	rmb();
	alloc = log->alloc;

	/*
		* Discard the line that was just read if the data could
		* have been corrupted by the producer.
		*/
	if (alloc - get > log->sz) {
		/*
		* this condition is acceptable in the case of the sfile sink
		* when attempting to read the oldest entry (at alloc-log->sz)
		* which may be overrun by a new one when ring buffer write index
		* wraps around. So the overrun is not reported in case the oldest line
		* was being read.
		*/
		if (sink->sfile && !oldest_line) {
			seq_printf(sink->sfile, "log overrun.\n",
				   s->line_buffer);
		}
		if (!sink->sfile) {
			dev_err(s->dev, "log overflow.\n");
		}
		sink->get = alloc - log->sz;
		return 0;
	}
	if (sink->sfile) {
		seq_printf(sink->sfile, "%s", s->line_buffer);
		sink->last_successful_next = sink->get;
	} else {
		if (trusty_panicked || __ratelimit(&trusty_log_rate_limit)) {
			dev_info(s->dev, "%s", s->line_buffer);
			sink->last_successful_next = sink->get; /* next line after last successful get */
		}
	}
	return 0;
}

static void *trusty_log_seq_start(struct seq_file *sfile, loff_t *pos)
{
	struct trusty_logbuffer *lb;
	struct trusty_log_state *s;
	struct log_rb *log;
	u32 index;

	lb = sfile->private;
	BUG_ON(!lb);
	s = container_of(lb, struct trusty_log_state, logbuffer);
	spin_lock(&s->logbuffer_sink.lock);
	s->logbuffer_sink.sfile = sfile;
	/*
	 * index vs pos
	 * pos is the offset from iterator's first entry
	 * when pos is 0, it needs to be converted to the unwrapped index: log->alloc-log-sz;
	 * this unwrapped index at start is recorded and used as described below.
	 * when pos > 0, it corresponds to the value returned by the last trusty_log_seq_next and is
	 * the delta offset from the begining of the iteration.
	 * To convert to the unwrapped index, it needs to be added to the
	 * unwrapped index recorded at the begining of the iteration.
	 */
	log = s->log;
	BUG_ON(!pos);
	if (*pos == 0) {
		s->logbuffer_sink.start =
			(log->alloc < log->sz) ? 0 : log->alloc - log->sz;
	}
	index = s->logbuffer_sink.start + *pos;
	dev_info(s->dev, "trusty_log_seq_start offset=%d start=%d", *pos,
		 s->logbuffer_sink.start);
	return trusty_log_start(s, &s->logbuffer_sink, index);
}

static void *trusty_log_seq_next(struct seq_file *sfile, void *v, loff_t *pos)
{
	struct trusty_logbuffer *lb;
	struct trusty_log_state *s;
	void *res;
	lb = sfile->private;
	BUG_ON(!lb);
	s = container_of(lb, struct trusty_log_state, logbuffer);
	res = trusty_log_next(s, &s->logbuffer_sink);
	if (!IS_ERR(res)) {
		BUG_ON(!pos);
		*pos = s->logbuffer_sink.get - s->logbuffer_sink.start;
	}
	if (*pos > 2 * s->log->sz) {
		BUG_ON(!s->logbuffer.misc.name);
		dev_err(s->dev,
			"/dev/%s sink aborted due to heavy trusty logging",
			s->logbuffer.misc.name);
		return NULL;
		// cannot catch-up with heavy logging on trusty side
	}
	return res;
}

static void trusty_log_seq_stop(struct seq_file *sfile, void *v)
{
	struct trusty_logbuffer *lb;
	struct trusty_log_state *s;
	lb = sfile->private;
	BUG_ON(!lb);
	s = container_of(lb, struct trusty_log_state, logbuffer);
	spin_unlock(&s->logbuffer_sink.lock);
}

static int trusty_log_seq_show(struct seq_file *sfile, void *v)
{
	struct trusty_logbuffer *lb;
	struct trusty_log_state *s;
	lb = sfile->private;
	BUG_ON(!lb);
	s = container_of(lb, struct trusty_log_state, logbuffer);
	return trusty_log_show(s, &s->logbuffer_sink);
}

static inline void trusty_log_logbuffer_init_config(struct trusty_log_state *s)
{
	s->logbuffer.cfg.dev = s->dev;
	s->logbuffer.cfg.id = s->dev->id;
	s->logbuffer.cfg.seq_ops.start = trusty_log_seq_start;
	s->logbuffer.cfg.seq_ops.stop = trusty_log_seq_stop;
	s->logbuffer.cfg.seq_ops.next = trusty_log_seq_next;
	s->logbuffer.cfg.seq_ops.show = trusty_log_seq_show;
	spin_lock_init(&s->logbuffer_sink.lock);
}

static inline int trusty_log_logbuffer_try_register(struct trusty_log_state *s)
{
	int rc;
	const char *name;
	name = dev_name(s->dev->parent);
	if (!name) {
		dev_err(s->dev,
			"/dev/logbuffer registration aborted due to unknown device name");
		return -ENAVAIL;
	}
	if (strlen(name) > 0) {
		strlcpy(s->logbuffer.cfg.name, name,
			sizeof(s->logbuffer.cfg.name));
	}
	if (*s->logbuffer.cfg.name == '\0') {
		dev_err(s->dev,
			"/dev/logbuffer registration aborted due to unknown device name");
		return -ENAVAIL;
	}
	trusty_log_logbuffer_init_config(s);
	rc = trusty_logbuffer_register(&s->logbuffer);
	if (rc < 0) {
		dev_err(s->dev, "/dev/logbuffer_%s registration failed\n",
			s->logbuffer.cfg.name);
		return rc;
	}
	return 0;
}

static void trusty_dump_logs(struct trusty_log_state *s, bool retry)
{
	void *get;
	u32 start;
	start = retry ? s->klog_sink.last_successful_next : s->klog_sink.get;
	get = trusty_log_start(s, &s->klog_sink, start);
	while (!IS_ERR_OR_NULL(get)) {
		trusty_log_show(s, &s->klog_sink);
		get = trusty_log_next(s, &s->klog_sink);
	}
}

static int trusty_log_call_notify(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct trusty_log_state *s;
	unsigned long flags;

	if (action != TRUSTY_CALL_RETURNED)
		return NOTIFY_DONE;

	s = container_of(nb, struct trusty_log_state, call_notifier);
	spin_lock_irqsave(&s->klog_sink.lock, flags);
	trusty_dump_logs(s, false);
	spin_unlock_irqrestore(&s->klog_sink.lock, flags);
	return NOTIFY_OK;
}

static int trusty_log_panic_notify(struct notifier_block *nb,
				   unsigned long action, void *data)
{
	struct trusty_log_state *s;

	/*
	 * Don't grab the spin lock to hold up the panic notifier, even
	 * though this is racy.
	 */
	s = container_of(nb, struct trusty_log_state, panic_notifier);
	dev_info(s->dev, "panic notifier - trusty version %s",
		 trusty_version_str_get(s->trusty_dev));
	trusty_dump_logs(s, true);
	return NOTIFY_OK;
}

static bool trusty_supports_logging(struct device *device)
{
	int result;

	result = trusty_std_call32(device, SMC_SC_SHARED_LOG_VERSION,
				   TRUSTY_LOG_API_VERSION, 0, 0);
	if (result == SM_ERR_UNDEFINED_SMC) {
		dev_info(device, "trusty-log not supported on secure side.\n");
		return false;
	} else if (result < 0) {
		dev_err(device,
			"trusty std call (SMC_SC_SHARED_LOG_VERSION) failed: %d\n",
			result);
		return false;
	}

	if (result != TRUSTY_LOG_API_VERSION) {
		dev_info(device, "unsupported api version: %d, supported: %d\n",
			 result, TRUSTY_LOG_API_VERSION);
		return false;
	}
	return true;
}

static int trusty_log_probe(struct platform_device *pdev)
{
	struct trusty_log_state *s;
	int result;
	trusty_shared_mem_id_t mem_id;

	if (!trusty_supports_logging(pdev->dev.parent))
		return -ENXIO;

	s = kzalloc(sizeof(*s), GFP_KERNEL);
	if (!s) {
		result = -ENOMEM;
		goto error_alloc_state;
	}

	spin_lock_init(&s->klog_sink.lock);
	s->dev = &pdev->dev;
	s->trusty_dev = s->dev->parent;
	s->log_pages = alloc_pages(GFP_KERNEL | __GFP_ZERO,
				   get_order(TRUSTY_LOG_SIZE));
	if (!s->log_pages) {
		result = -ENOMEM;
		goto error_alloc_log;
	}
	s->log = page_address(s->log_pages);

	sg_init_one(&s->sg, s->log, TRUSTY_LOG_SIZE);
	result = trusty_share_memory_compat(s->trusty_dev, &mem_id, &s->sg, 1,
					    PAGE_KERNEL);
	if (result) {
		dev_err(s->dev, "trusty_share_memory failed: %d\n", result);
		goto err_share_memory;
	}
	s->log_pages_shared_mem_id = mem_id;

	result = trusty_std_call32(s->trusty_dev,
				   SMC_SC_SHARED_LOG_ADD,
				   (u32)(mem_id), (u32)(mem_id >> 32),
				   TRUSTY_LOG_SIZE);
	if (result < 0) {
		dev_err(s->dev,
			"trusty std call (SMC_SC_SHARED_LOG_ADD) failed: %d 0x%llx\n",
			result, mem_id);
		goto error_std_call;
	}

	s->call_notifier.notifier_call = trusty_log_call_notify;
	result = trusty_call_notifier_register(s->trusty_dev,
					       &s->call_notifier);
	if (result < 0) {
		dev_err(&pdev->dev,
			"failed to register trusty call notifier\n");
		goto error_call_notifier;
	}

	s->panic_notifier.notifier_call = trusty_log_panic_notify;
	result = atomic_notifier_chain_register(&panic_notifier_list,
						&s->panic_notifier);
	if (result < 0) {
		dev_err(&pdev->dev,
			"failed to register panic notifier\n");
		goto error_panic_notifier;
	}
	result = trusty_log_logbuffer_try_register(s);
	if (result < 0) {
		dev_err(&pdev->dev, "failed to register logbuffer\n");
		goto error_logbuffer;
	}
	platform_set_drvdata(pdev, s);
	return 0;

error_logbuffer:
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &s->panic_notifier);
error_panic_notifier:
	trusty_call_notifier_unregister(s->trusty_dev, &s->call_notifier);
error_call_notifier:
	trusty_std_call32(s->trusty_dev, SMC_SC_SHARED_LOG_RM,
			  (u32)mem_id, (u32)(mem_id >> 32), 0);
error_std_call:
	if (WARN_ON(trusty_reclaim_memory(s->trusty_dev, mem_id, &s->sg, 1))) {
		dev_err(&pdev->dev, "trusty_revoke_memory failed: %d 0x%llx\n",
			result, mem_id);
		/*
		 * It is not safe to free this memory if trusty_revoke_memory
		 * fails. Leak it in that case.
		 */
	} else {
err_share_memory:
		__free_pages(s->log_pages, get_order(TRUSTY_LOG_SIZE));
	}
error_alloc_log:
	kfree(s);
error_alloc_state:
	return result;
}

static int trusty_log_remove(struct platform_device *pdev)
{
	int result;
	struct trusty_log_state *s = platform_get_drvdata(pdev);
	trusty_shared_mem_id_t mem_id = s->log_pages_shared_mem_id;

	trusty_logbuffer_unregister(&s->logbuffer);
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &s->panic_notifier);
	trusty_call_notifier_unregister(s->trusty_dev, &s->call_notifier);

	result = trusty_std_call32(s->trusty_dev, SMC_SC_SHARED_LOG_RM,
				   (u32)mem_id, (u32)(mem_id >> 32), 0);
	if (result) {
		dev_err(&pdev->dev,
			"trusty std call (SMC_SC_SHARED_LOG_RM) failed: %d\n",
			result);
	}
	result = trusty_reclaim_memory(s->trusty_dev, mem_id, &s->sg, 1);
	if (WARN_ON(result)) {
		dev_err(&pdev->dev,
			"trusty failed to remove shared memory: %d\n", result);
	} else {
		/*
		 * It is not safe to free this memory if trusty_revoke_memory
		 * fails. Leak it in that case.
		 */
		__free_pages(s->log_pages, get_order(TRUSTY_LOG_SIZE));
	}
	kfree(s);

	return 0;
}

static const struct of_device_id trusty_test_of_match[] = {
	{ .compatible = "android,trusty-log-v1", },
	{},
};

MODULE_DEVICE_TABLE(trusty, trusty_test_of_match);

static struct platform_driver trusty_log_driver = {
	.probe = trusty_log_probe,
	.remove = trusty_log_remove,
	.driver = {
		.name = "trusty-log",
		.of_match_table = trusty_test_of_match,
	},
};

module_platform_driver(trusty_log_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Trusty logging driver");
