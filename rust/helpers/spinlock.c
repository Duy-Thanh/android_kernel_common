// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/spinlock.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper void rust_helper___spin_lock_init(spinlock_t *lock,
						const char *name,
						struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_SPINLOCK
# if defined(CONFIG_PREEMPT_RT)
	__spin_lock_init(lock, name, key, false);
# else /*!CONFIG_PREEMPT_RT */
	__raw_spin_lock_init(spinlock_check(lock), name, key, LD_WAIT_CONFIG);
# endif /* CONFIG_PREEMPT_RT */
#else /* !CONFIG_DEBUG_SPINLOCK */
	spin_lock_init(lock);
#endif /* CONFIG_DEBUG_SPINLOCK */
}

__rust_helper void rust_helper_spin_lock(spinlock_t *lock)
{
	spin_lock(lock);
}

__rust_helper void rust_helper_spin_unlock(spinlock_t *lock)
{
	spin_unlock(lock);
}

__rust_helper int rust_helper_spin_trylock(spinlock_t *lock)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
void rust_helper___spin_lock_init(spinlock_t *lock, const char *name,
				  struct lock_class_key *key)
{
#ifdef CONFIG_DEBUG_SPINLOCK
# if defined(CONFIG_PREEMPT_RT)
	__spin_lock_init(lock, name, key, false);
# else /*!CONFIG_PREEMPT_RT */
	__raw_spin_lock_init(spinlock_check(lock), name, key, LD_WAIT_CONFIG);
# endif /* CONFIG_PREEMPT_RT */
#else /* !CONFIG_DEBUG_SPINLOCK */
	spin_lock_init(lock);
#endif /* CONFIG_DEBUG_SPINLOCK */
}

void rust_helper_spin_lock(spinlock_t *lock)
{
	spin_lock(lock);
}

void rust_helper_spin_unlock(spinlock_t *lock)
{
	spin_unlock(lock);
}

int rust_helper_spin_trylock(spinlock_t *lock)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	return spin_trylock(lock);
}
