// SPDX-License-Identifier: GPL-2.0

#include <linux/list_lru.h>
#include <linux/task_work.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper unsigned long rust_helper_list_lru_count(struct list_lru *lru)
{
	return list_lru_count(lru);
}

__rust_helper unsigned long rust_helper_list_lru_walk(struct list_lru *lru,
					list_lru_walk_cb isolate, void *cb_arg,
					unsigned long nr_to_walk)
{
	return list_lru_walk(lru, isolate, cb_arg, nr_to_walk);
}

__rust_helper void rust_helper_init_task_work(struct callback_head *twork,
					task_work_func_t func)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
unsigned long rust_helper_list_lru_count(struct list_lru *lru)
{
	return list_lru_count(lru);
}

unsigned long rust_helper_list_lru_walk(struct list_lru *lru,
					list_lru_walk_cb isolate, void *cb_arg,
					unsigned long nr_to_walk)
{
	return list_lru_walk(lru, isolate, cb_arg, nr_to_walk);
}

void rust_helper_init_task_work(struct callback_head *twork,
				task_work_func_t func)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	init_task_work(twork, func);
}
