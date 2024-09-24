// SPDX-License-Identifier: GPL-2.0

#include <linux/mman.h>

unsigned long rust_helper_calc_vm_prot_bits(unsigned long prot, unsigned long pkey)
{
	return calc_vm_prot_bits(prot, pkey);
}

void rust_helper_lockdep_set_class_rwsem(struct rw_semaphore *lock, struct lock_class_key *key,
					 const char *name)
{
	lockdep_set_class_and_name(lock, key, name);
}
