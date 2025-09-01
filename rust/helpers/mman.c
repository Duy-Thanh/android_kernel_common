// SPDX-License-Identifier: GPL-2.0

#include <linux/mman.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
void rust_helper_lockdep_set_class_rwsem(struct rw_semaphore *lock, struct lock_class_key *key,
					 const char *name)
{
	lockdep_set_class_and_name(lock, key, name);
}
