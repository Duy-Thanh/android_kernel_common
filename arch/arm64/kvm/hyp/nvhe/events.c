// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Google LLC
 */

#include <nvhe/mm.h>
#include <nvhe/trace.h>

#include <nvhe/define_events.h>

extern struct hyp_event_id __hyp_event_ids_start[];
extern struct hyp_event_id __hyp_event_ids_end[];

#define MAX_EVENT_ID_MOD 128

static atomic_t num_event_id_mod = ATOMIC_INIT(0);
static DEFINE_HYP_SPINLOCK(event_id_mod_lock);
static struct {
	struct hyp_event_id	*start;
	struct hyp_event_id	*end;
} event_id_mod[MAX_EVENT_ID_MOD];

static void __hyp_set_key(phys_addr_t key_pa, int val)
{
	atomic_t *key = hyp_fixmap_map(key_pa);

	atomic_set(key, val);
	hyp_fixmap_unmap();
}

static void hyp_set_key(atomic_t *key, int val)
{
	__hyp_set_key(__hyp_pa(key), val);
}

static void hyp_mod_set_key(atomic_t *key, int val)
{
	__hyp_set_key(__pkvm_module_pa(key), val);
}

static bool __try_set_event(unsigned short id, bool enable,
			    struct hyp_event_id *event_id,
			    struct hyp_event_id *end,
			    void (*set_key)(atomic_t *, int))
{
	atomic_t *enable_key;

	for (; event_id < end; event_id++) {
		if (event_id->id != id)
			continue;

		enable_key = (atomic_t *)event_id->data;
		set_key(enable_key, enable);

		return true;
	}

	return false;
}

static bool try_set_event(unsigned short id, bool enable)
{
	return __try_set_event(id, enable, __hyp_event_ids_start,
			       __hyp_event_ids_end, hyp_set_key);
}

static bool try_set_mod_event(unsigned short id, bool enable)
{
	int i, nr_mod;

	/*
	 * Order access between num_event_id_mod and event_id_mod.
	 * Paired with register_hyp_event_ids()
	 */
	nr_mod = atomic_read_acquire(&num_event_id_mod);

	for (i = 0; i < nr_mod; i++) {
		if (__try_set_event(id, enable, event_id_mod[i].start,
				    event_id_mod[i].end, hyp_mod_set_key))
			return true;
	}

	return false;
}

int register_hyp_event_ids(unsigned long start, unsigned long end)
{
	int mod, ret = -ENOMEM;

	hyp_spin_lock(&event_id_mod_lock);

	mod = atomic_read(&num_event_id_mod);
	if (mod < MAX_EVENT_ID_MOD) {
		event_id_mod[mod].start = (struct hyp_event_id *)start;
		event_id_mod[mod].end = (struct hyp_event_id *)end;
		/*
		 * Order access between num_event_id_mod and event_id_mod.
		 * Paired with try_set_mod_event()
		 */
		atomic_set_release(&num_event_id_mod, mod + 1);
		ret = 0;
	}

	hyp_spin_unlock(&event_id_mod_lock);

	return ret;
}

int __pkvm_enable_event(unsigned short id, bool enable)
{
	if (try_set_event(id, enable))
		return 0;

	if (try_set_mod_event(id, enable))
		return 0;

	return -EINVAL;
}
