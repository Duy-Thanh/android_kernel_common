// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/workqueue.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper void rust_helper_init_work_with_key(struct work_struct *work,
						  work_func_t func,
						  bool onstack,
						  const char *name,
						  struct lock_class_key *key)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
void rust_helper_init_work_with_key(struct work_struct *work, work_func_t func,
				    bool onstack, const char *name,
				    struct lock_class_key *key)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	__init_work(work, onstack);
	work->data = (atomic_long_t)WORK_DATA_INIT();
	lockdep_init_map(&work->lockdep_map, name, key, 0);
	INIT_LIST_HEAD(&work->entry);
	work->func = func;
}
