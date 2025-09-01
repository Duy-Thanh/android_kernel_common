// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/mutex.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper void rust_helper_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}

__rust_helper void rust_helper___mutex_init(struct mutex *mutex, const char *name,
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
void rust_helper_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}

void rust_helper___mutex_init(struct mutex *mutex, const char *name,
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
			      struct lock_class_key *key)
{
	__mutex_init(mutex, name, key);
}
