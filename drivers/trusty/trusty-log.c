// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Google, Inc.
 */
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

struct trusty_log_state {
	struct device *dev;
	struct device *trusty_dev;

	/*
	 * This lock is here to ensure only one consumer will read
	 * from the log ring buffer at a time.
	 */
	spinlock_t lock;
	struct log_rb *log;
	u32 get;

	struct page *log_pages;
	struct scatterlist sg;
	trusty_shared_mem_id_t log_pages_shared_mem_id;

	struct notifier_block call_notifier;
	struct notifier_block panic_notifier;
	char line_buffer[TRUSTY_LINE_BUFFER_SIZE];
};

static char *trusty_get_app_name(struct trusty_log_state *s, int32_t trusty_app_id)
{
        int ret;
        int i;
        int app_name_len;
        char *app_name = NULL;

        if (trusty_app_id == -1) {
                ret = SM_ERR_INVALID_PARAMETERS;
                goto err_get_name;
        }

        ret = trusty_fast_call32(s->dev, SMC_SC_SHARED_LOG_GET_TRUSTY_APP_NAME,
                          -1, trusty_app_id, 0);
        if (ret <= 0)
                goto err_get_name;

        app_name_len = ret;

        app_name = kmalloc(app_name_len + 1, GFP_KERNEL);
        for (i = 0; i < app_name_len; i++) {
                ret = trusty_fast_call32(s->dev, SMC_SC_SHARED_LOG_GET_TRUSTY_APP_NAME,
                                        i, trusty_app_id, 0);
                if (ret < 0)
                        goto err_get_char;
                app_name[i] = ret;
        }
        app_name[i] = '\0';

        return app_name;

err_get_char:
        kfree(app_name);
        app_name = NULL;
err_get_name:
        dev_info(s->dev, "Error while getting app name from its id, error: %d\n", ret);
        return app_name;
}

static int log_read_line(struct trusty_log_state *s, int put, int get)
{
    int i, j;
    char c = '\0';
    uint32_t offset, len;
    uint32_t size_rb = s->log->sz;

    i = 0;
    while (i < sizeof(s->line_buffer) - 1 &&
           get < put) {
        offset = get & (size_rb - 1);
        len = (uint32_t)s->log->data[offset];
        /* each entry consist of {size + data + metadata + size} */
        get = get + sizeof(uint32_t) + len +
            sizeof(struct log_metadata) + sizeof(uint32_t);

        offset = offset + sizeof(uint32_t);
        for (j = 0; j < len; j++) {
            s->line_buffer[i++] = c = s->log->data[offset + j];
            if ( c == '\n' || i >= sizeof(s->line_buffer) - 1)
                break;
        }
        if (c == '\n')
            break;
    }
    s->line_buffer[i] = '\0';

    return get;
}

static void trusty_dump_logs(struct trusty_log_state *s)
{
	struct log_rb *log = s->log;
	u32 get, put, alloc, entry;
        uint32_t offset, len;
        uint32_t size_rb = s->log->sz;
        uint32_t entry_size;
        volatile struct log_metadata *metadata;

	if (WARN_ON(!is_power_of_2(log->sz)))
		return;

	/*
	 * For this ring buffer, at any given point, alloc >= put >= get.
	 * The producer side of the buffer is not locked, so the put and alloc
	 * pointers must be read in a defined order (put before alloc) so
	 * that the above condition is maintained. A read barrier is needed
	 * to make sure the hardware and compiler keep the reads ordered.
	 */
	get = s->get;
	while ((put = log->put) != get) {
		/* Make sure that the read of put occurs before the read of log data */
		rmb();

                offset = get & (size_rb - 1);
                len = (uint32_t)s->log->data[offset];

                offset = offset + sizeof(uint32_t) + len;
                metadata = (struct log_metadata *)&s->log->data[offset];

                /* Read a line from the log */
		get = log_read_line(s, put, get);

		/* Force the loads from log_read_line to complete. */
		rmb();
		alloc = log->alloc;

		/*
		 * Discard the line that was just read if the data could
		 * have been corrupted by the producer.
		 */
                if (alloc - get > log->sz) {
                    dev_err(s->dev, "log overflow.");
                    entry = alloc;
                    do {
                        offset = entry & (size_rb - 1);

                        /* find previous log data length from footer*/
                        offset -= sizeof(uint32_t);
                        len = (uint32_t)s->log->data[offset];

                        /* total size of entry */
                        entry_size = sizeof(uint32_t) +
                                len + sizeof(struct log_metadata) + sizeof(uint32_t);

                        entry = entry - entry_size;
                    } while (entry > (alloc - log->sz));

                    get = entry;
                    continue;
                }

                if (__ratelimit(&trusty_log_rate_limit)) {
                    char *app_name = trusty_get_app_name(s, metadata->app_id);
                    if (app_name != NULL) {
                        dev_info(s->dev, "%llu: %s: %s",
                                 metadata->timestamp,
                                 app_name,
                                 s->line_buffer);
                        kfree(app_name);
                    } else {
                        dev_info(s->dev, "%llu: %u: %s",
                                 metadata->timestamp,
                                 metadata->app_id,
                                 s->line_buffer);
                    }
                }
	}
	s->get = get;
}

static int trusty_log_call_notify(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct trusty_log_state *s;
	unsigned long flags;

	if (action != TRUSTY_CALL_RETURNED)
		return NOTIFY_DONE;

	s = container_of(nb, struct trusty_log_state, call_notifier);
	spin_lock_irqsave(&s->lock, flags);
	trusty_dump_logs(s);
	spin_unlock_irqrestore(&s->lock, flags);
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
	trusty_dump_logs(s);
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

	spin_lock_init(&s->lock);
	s->dev = &pdev->dev;
	s->trusty_dev = s->dev->parent;
	s->get = 0;
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
	platform_set_drvdata(pdev, s);

	return 0;

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
