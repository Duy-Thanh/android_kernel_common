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

/* list of application names */
struct log_trusty_app {
    struct list_head node;
    int32_t app_id;
    char *name;
};
LIST_HEAD(app_names_list);

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

static int log_alloc_app_name(struct trusty_log_state *s,
                              char *name, int32_t app_id,
                              struct list_head *list) {
    int ret = -ENOMEM;
    struct log_trusty_app *log_tapp;
    struct log_trusty_app *obj, *next_obj;

    if (!name) {
        ret = -EINVAL;
        goto err_alloc_log;
    }

    list_for_each_entry_safe(obj, next_obj, &app_names_list, node) {
        if (strcmp(obj->name, name) == 0) {
            goto already_name_exist;
        }
    }

    log_tapp = kzalloc(sizeof(*log_tapp), GFP_KERNEL);
    if (!log_tapp)
        goto err_alloc_log;

    log_tapp->name = kzalloc((strlen(name) + 1), GFP_KERNEL);
    if (!log_tapp->name)
        goto err_alloc_name;

    strcpy(log_tapp->name, name);
    log_tapp->app_id = app_id;

    list_add_tail(&log_tapp->node, list);
already_name_exist:
    return 0;
err_alloc_name:
    kfree(log_tapp);
err_alloc_log:
    return ret;
}

static void log_free_app_name(struct log_trusty_app *log_tapp) {
    list_del(&log_tapp->node);
    kfree(log_tapp->name);
    kfree(log_tapp);
}

static inline uint32_t get_offset_of_app_name(uint32_t base, uint32_t log_len) {
    return (base + sizeof(uint32_t) + log_len);
}

static inline uint32_t get_offset_of_log_text(uint32_t base) {
    return (base + sizeof(uint32_t));
}

static inline uint32_t get_offset_of_metadata(uint32_t base,
                                              uint32_t log_len,
                                              uint32_t app_name_len) {
    return (base + sizeof(uint32_t) + log_len + app_name_len);
}

static inline uint32_t get_offset_of_app_name_len(uint32_t base,
                                                  uint32_t entry_size) {
    return (base + entry_size - sizeof(uint32_t) - sizeof(uint32_t));
}

static inline uint32_t get_offset_of_log_text_len(uint32_t base,
                                                  uint32_t entry_size) {
    return (base + entry_size - sizeof(uint32_t));
}

static inline uint32_t get_min_log_entry_size() {
  return (sizeof(struct log_metadata) + (2 * sizeof(uint32_t)));
}

/* Finds the previous entry size from current entry offset */
static uint32_t get_previous_entry_size(struct trusty_log_state *s,
                                        uint32_t offset) {
    uint32_t log_len, app_name_len;

    offset -= sizeof(uint32_t);
    log_len = (uint32_t)s->log->data[offset];

    offset -= sizeof(uint32_t);
    app_name_len = (uint32_t)s->log->data[offset];

    return (sizeof(uint32_t) + log_len + app_name_len +
            sizeof(struct log_metadata) + sizeof(uint32_t) + sizeof(uint32_t));
}

static volatile char *get_app_name_from_log(struct log_rb *log,
                                   uint32_t offset,
                                   uint32_t entry_size) {
    volatile char *app_name = NULL;

    /* get app-name-size from footer */
    uint32_t app_name_len = (uint32_t)log->data[
            get_offset_of_app_name_len(offset, entry_size)];

    /* get app_name */
    if (app_name_len > 0) {
        uint32_t log_len = (uint32_t)log->data[
                get_offset_of_log_text_len(offset, entry_size)];
        app_name = &log->data[
                get_offset_of_app_name(offset, log_len)];
    }

    return app_name;
}

static struct log_metadata* get_metadata_from_log(struct log_rb *log,
                                   uint32_t offset,
                                   uint32_t entry_size) {
    uint32_t app_name_len = (uint32_t)log->data[
            get_offset_of_app_name_len(offset, entry_size)];
    uint32_t log_len = (uint32_t)log->data[
            get_offset_of_log_text_len(offset, entry_size)];
    return ((struct log_metadata *)&log->data[
            get_offset_of_metadata(offset, app_name_len, log_len)]);

}

static void print_log_buffer(struct trusty_log_state *s,
                                volatile char *app_name,
                                volatile struct log_metadata *metadata) {
    if (__ratelimit(&trusty_log_rate_limit)) {
        if (app_name) {
            dev_info(s->dev, "%llu: %s: %s",
                     metadata->timestamp,
                     app_name,
                     s->line_buffer);
        } else {
            dev_info(s->dev, "%llu: %s",
                     metadata->timestamp,
                     s->line_buffer);
        }
    }
}

static void handle_partial_log_entry(struct trusty_log_state *s,
                                     uint32_t valid_entry, u32 alloc) {
    char c = '\0';
    int32_t offset, log_len, app_name_len, i;
    int32_t partial_log_len;
    struct log_metadata *metadata;
    volatile char *app_name = NULL;
    uint32_t size_rb = s->log->sz;
    int32_t partial_entry_size = valid_entry - (alloc - size_rb);

    if (partial_entry_size > get_min_log_entry_size()) {
        offset = valid_entry & (size_rb - 1);

        offset -= sizeof(uint32_t);
        log_len = (uint32_t)s->log->data[offset];

        offset -= sizeof(uint32_t);
        app_name_len = (uint32_t)s->log->data[offset];

        partial_log_len =
                partial_entry_size - get_min_log_entry_size() - app_name_len;

        if (partial_log_len > 0) {
            offset -= sizeof(struct log_metadata);
            metadata = (struct log_metadata *)&s->log->data[offset];

            offset -= app_name_len;
            if (app_name_len > 0) {
                app_name = &s->log->data[offset];
            }

            if (partial_log_len > log_len)
                partial_log_len = log_len;

            offset -= partial_log_len;
            for (i = 0; i < partial_log_len; i++) {
                s->line_buffer[i] = c = s->log->data[offset + i];
                if ( c == '\n' || i >= sizeof(s->line_buffer) - 1)
                    break;
            }
            s->line_buffer[i] = '\0';

            if (app_name) {
                log_alloc_app_name(s, (char *)app_name, metadata->app_id,
                                   &app_names_list);
            }

            /* check for overflow */
            alloc = s->log->alloc;
            if (alloc - valid_entry > size_rb) {
                dev_err(s->dev, "Log overflow.");
                return;
            }

            print_log_buffer(s, app_name, metadata);
        }
    }
}

/*
 * each entry consist of
 * {
 *   size_of_this_entry + log_string + app_name +
 *   metadata + app_name_size + log_string_size
 * }
 */
static int log_read_line(struct trusty_log_state *s, u32 put, u32 get)
{
    int i, j;
    char c = '\0';
    uint32_t offset, len, entry_size;
    uint32_t size_rb = s->log->sz;

    i = 0;
    while (i < sizeof(s->line_buffer) - 1 &&
           get < put) {
        /* check for overflow */
        if (s->log->alloc - get > s->log->sz) {
            return get;
        }

        offset = get & (size_rb - 1);
        if (get == s->log->watermark_entry) {
            get += (size_rb - offset);
            offset = get & (size_rb - 1);
            continue;
        }

        entry_size = (uint32_t)s->log->data[offset];
        get += entry_size;

        /* get log string from log */
        len = (uint32_t)s->log->data[
                get_offset_of_log_text_len(offset, entry_size)];
        offset = get_offset_of_log_text(offset);
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

static uint32_t handle_log_overflow(struct trusty_log_state *s, uint32_t alloc) {
    uint32_t entry, valid_entry, watermark_entry;
    uint32_t entry_size;
    uint32_t offset;
    struct log_rb *log = s->log;
    entry = valid_entry = watermark_entry = log->watermark_entry;

    while ((entry - get_min_log_entry_size()) >= (alloc - log->sz)) {
        if ((alloc != log->alloc) ||
            (watermark_entry != log->watermark_entry)) {
            alloc = log->alloc;
            entry = valid_entry = watermark_entry = log->watermark_entry;
            continue;
        }

        offset = entry & (log->sz - 1);
        entry_size = get_previous_entry_size(s, offset);
        entry = entry - entry_size;
        if (entry >= (alloc - log->sz)) {
            valid_entry = entry;
        }
    }

    if (valid_entry - (alloc - log->sz) > get_min_log_entry_size()) {
        handle_partial_log_entry(s, valid_entry, alloc);
    }

    return valid_entry;
}

static void trusty_dump_logs(struct trusty_log_state *s)
{
	struct log_rb *log = s->log;
        u32 get, put, alloc;
        uint32_t offset;
        uint32_t size_rb = s->log->sz;
        uint32_t entry_size;
        volatile struct log_metadata *metadata;
        volatile char *app_name;

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
                if (get == s->log->watermark_entry) {
                    get += (size_rb - offset);
                    continue;
                }
                entry_size = (uint32_t)s->log->data[offset];
                app_name = get_app_name_from_log(s->log, offset, entry_size);
                metadata = get_metadata_from_log(s->log, offset, entry_size);
                if (app_name) {
                    log_alloc_app_name(s, (char *)app_name, metadata->app_id,
                                       &app_names_list);
                }

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
                    get = handle_log_overflow(s, alloc);
                    continue;
                }

                print_log_buffer(s, app_name, metadata);
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
        struct log_trusty_app *obj, *next_obj;
	struct trusty_log_state *s = platform_get_drvdata(pdev);
	trusty_shared_mem_id_t mem_id = s->log_pages_shared_mem_id;

        list_for_each_entry_safe(obj, next_obj, &app_names_list, node) {
            log_free_app_name(obj);
        }

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
