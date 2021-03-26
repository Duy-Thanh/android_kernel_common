/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Linaro Ltd
 *
 * Author: Daniel Lezcano <daniel.lezcano@linaro.org>
 */
#ifndef ___DTPM_H__
#define ___DTPM_H__

#include <linux/powercap.h>

#define MAX_DTPM_DESCR 8
#define MAX_DTPM_CONSTRAINTS 1

struct dtpm {
	struct powercap_zone zone;
	struct kref kref;
	struct dtpm *parent;
	struct list_head sibling;
	struct list_head children;
	struct dtpm_ops *ops;
	unsigned long flags;
	u64 power_limit;
	u64 power_max;
	u64 power_min;
	int weight;

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
	ANDROID_KABI_RESERVE(3);
	ANDROID_KABI_RESERVE(4);
	ANDROID_KABI_RESERVE(5);
};

struct dtpm_ops {
	u64 (*set_power_uw)(struct dtpm *, u64);
	u64 (*get_power_uw)(struct dtpm *);
	int (*update_power_uw)(struct dtpm *);
	void (*release)(struct dtpm *);
};

typedef int (*dtpm_init_t)(void);

struct dtpm_descr {
	dtpm_init_t init;
};

/* Init section thermal table */
extern struct dtpm_descr __dtpm_table[];
extern struct dtpm_descr __dtpm_table_end[];

#define DTPM_TABLE_ENTRY(name, __init)				\
	static struct dtpm_descr __dtpm_table_entry_##name	\
	__used __section("__dtpm_table") = {			\
		.init = __init,					\
	}

#define DTPM_DECLARE(name, init)	DTPM_TABLE_ENTRY(name, init)

#define for_each_dtpm_table(__dtpm)	\
	for (__dtpm = __dtpm_table;	\
	     __dtpm < __dtpm_table_end;	\
	     __dtpm++)

static inline struct dtpm *to_dtpm(struct powercap_zone *zone)
{
	return container_of(zone, struct dtpm, zone);
}

int dtpm_update_power(struct dtpm *dtpm);

int dtpm_release_zone(struct powercap_zone *pcz);

void dtpm_init(struct dtpm *dtpm, struct dtpm_ops *ops);

void dtpm_destroy(struct dtpm *dtpm);

int dtpm_create(const char *name, struct dtpm *dtpm, struct dtpm *parent);

int dtpm_register(const char *name, struct dtpm *dtpm);

void dtpm_unregister(const char *name);

struct dtpm *dtpm_get(const char *name);

void dtpm_put(struct dtpm *dtpm);
#endif
