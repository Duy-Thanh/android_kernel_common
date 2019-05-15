// SPDX-License-Identifier: GPL-2.0
/*
 * keyslot-manager.c
 *
 * Copyright 2019 Google LLC
 */

/**
 * DOC: The Keyslot Manager
 *
 * Many devices with inline encryption support have a limited number of "slots"
 * into which encryption contexts may be programmed, and requests can be tagged
 * with a slot number to specify the key to use for en/decryption.
 *
 * As the number of slots are limited, and programming keys is expensive on
 * many inline encryption hardware, we don't want to program the same key into
 * multiple slots - if multiple requests are using the same key, we want to
 * program just one slot with that key and use that slot for all requests.
 *
 * The keyslot manager manages these keyslots appropriately, and also acts as
 * an abstraction between the inline encryption hardware and the upper layers.
 *
 * Lower layer devices will set up a keyslot manager in their request queue
 * and tell it how to perform device specific operations like programming/
 * evicting keys from keyslots.
 *
 * Upper layers will call keyslot_manager_get_slot_for_key() to program a
 * key into some slot in the inline encryption hardware.
 */
#include <linux/slab.h>
#include <linux/keyslot-manager.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/wait.h>

struct keyslot_manager {
	unsigned int num_slots;
	atomic_t num_idle_slots;
	struct keyslot_mgmt_ll_ops ksm_ll_ops;
	void *ll_priv_data;
	struct mutex lock;
	wait_queue_head_t wait_queue;
	u64 seq_num;
	u64 *last_used_seq_nums;
	atomic_t slot_refs[];
};

/**
 * keyslot_manager_create() - Create a keyslot manager
 * @num_slots: The number of key slots to manage.
 * @ksm_ll_ops: The struct keyslot_mgmt_ll_ops for the device that this keyslot
 *		manager will use to perform operations like programming and
 *		evicting keys.
 * @ll_priv_data: Private data passed as is to the functions in ksm_ll_ops.
 *
 * Allocate memory for and initialize a keyslot manager. Called by for e.g.
 * storage drivers to set up a keyslot manager in their request_queue.
 *
 * Context: May sleep
 * Return: Pointer to constructed keyslot manager or NULL on error.
 */
struct keyslot_manager *keyslot_manager_create(unsigned int num_slots,
				const struct keyslot_mgmt_ll_ops *ksm_ll_ops,
				void *ll_priv_data)
{
	struct keyslot_manager *ksm;

	if (num_slots == 0)
		return NULL;

	/* Check that all ops are specified */
	if (ksm_ll_ops->keyslot_program == NULL ||
	    ksm_ll_ops->keyslot_evict == NULL ||
	    ksm_ll_ops->crypt_mode_supported == NULL ||
	    ksm_ll_ops->keyslot_find == NULL)
		return NULL;

	ksm = kzalloc(struct_size(ksm, slot_refs, num_slots), GFP_KERNEL);
	if (!ksm)
		return NULL;

	ksm->num_slots = num_slots;
	atomic_set(&ksm->num_idle_slots, num_slots);
	ksm->ksm_ll_ops = *ksm_ll_ops;
	ksm->ll_priv_data = ll_priv_data;

	mutex_init(&ksm->lock);
	init_waitqueue_head(&ksm->wait_queue);

	ksm->last_used_seq_nums = kcalloc(num_slots, sizeof(u64), GFP_KERNEL);
	if (!ksm->last_used_seq_nums) {
		kzfree(ksm);
		ksm = NULL;
	}

	return ksm;
}
EXPORT_SYMBOL(keyslot_manager_create);

/**
 * keyslot_manager_get_slot_for_key() - Program a key into a keyslot.
 * @ksm: The keyslot manager to program the key into.
 * @key: Pointer to the bytes of the key to program. Must be the correct length
 *      for the chosen @crypt_mode; see blk_crypt_modes in blk-crypto.c.
 * @crypt_mode: Identifier for the encryption algorithm to use.
 * @data_unit_size: The data unit size to use for en/decryption.
 *
 * Get a keyslot that's been programmed with the specified key, crypt_mode, and
 * data_unit_size.  If one already exists, return it with incremented refcount.
 * Otherwise, wait for a keyslot to become idle and program it.
 *
 * Context: Process context. Takes and releases ksm->lock.
 * Return: The keyslot on success, else a -errno value.
 */
int keyslot_manager_get_slot_for_key(struct keyslot_manager *ksm,
				     const u8 *key,
				     enum blk_crypt_mode_num crypt_mode,
				     unsigned int data_unit_size)
{
	int slot;
	int err;
	int c;
	int lru_idle_slot;
	u64 min_seq_num;

	mutex_lock(&ksm->lock);
	slot = ksm->ksm_ll_ops.keyslot_find(ksm->ll_priv_data, key,
					    crypt_mode,
					    data_unit_size);

	if (slot < 0 && slot != -ENOKEY) {
		mutex_unlock(&ksm->lock);
		return slot;
	}

	if (WARN_ON(slot >= (int)ksm->num_slots)) {
		mutex_unlock(&ksm->lock);
		return -EINVAL;
	}

	/* Try to use the returned slot */
	if (slot != -ENOKEY) {
		/*
		 * NOTE: We may fail to get a slot if the number of refs
		 * overflows UINT_MAX. I don't think we care enough about
		 * that possibility to make the refcounts u64, considering
		 * the only way for that to happen is for at least UINT_MAX
		 * requests to be in flight at the same time.
		 */
		if ((unsigned int)atomic_read(&ksm->slot_refs[slot]) ==
		    UINT_MAX) {
			mutex_unlock(&ksm->lock);
			return -EBUSY;
		}

		if (atomic_fetch_inc(&ksm->slot_refs[slot]) == 0)
			atomic_dec(&ksm->num_idle_slots);

		ksm->last_used_seq_nums[slot] = ++ksm->seq_num;

		mutex_unlock(&ksm->lock);
		return slot;
	}

	/*
	 * If we're here, that means there wasn't a slot that
	 * was already programmed with the key
	 */

	/* Wait till there is a free slot available */
	while (atomic_read(&ksm->num_idle_slots) == 0) {
		mutex_unlock(&ksm->lock);
		wait_event(ksm->wait_queue,
			   (atomic_read(&ksm->num_idle_slots) > 0));
		mutex_lock(&ksm->lock);
	}

	/* Todo: fix linear scan? */
	/* Find least recently used idle slot (i.e. slot with minimum number) */
	lru_idle_slot  = -1;
	min_seq_num = 0;
	for (c = 0; c < ksm->num_slots; c++) {
		if (atomic_read(&ksm->slot_refs[c]) != 0)
			continue;

		if (lru_idle_slot == -1 ||
		    ksm->last_used_seq_nums[c] < min_seq_num) {
			lru_idle_slot = c;
			min_seq_num = ksm->last_used_seq_nums[c];
		}
	}

	if (WARN_ON(lru_idle_slot == -1)) {
		mutex_unlock(&ksm->lock);
		return -EBUSY;
	}

	atomic_dec(&ksm->num_idle_slots);
	atomic_inc(&ksm->slot_refs[lru_idle_slot]);
	err = ksm->ksm_ll_ops.keyslot_program(ksm->ll_priv_data, key,
					      crypt_mode,
					      data_unit_size,
					      lru_idle_slot);

	if (err) {
		atomic_dec(&ksm->slot_refs[lru_idle_slot]);
		atomic_inc(&ksm->num_idle_slots);
		wake_up(&ksm->wait_queue);
		mutex_unlock(&ksm->lock);
		return err;
	}

	ksm->seq_num++;
	ksm->last_used_seq_nums[lru_idle_slot] = ksm->seq_num;

	mutex_unlock(&ksm->lock);
	return lru_idle_slot;
}
EXPORT_SYMBOL(keyslot_manager_get_slot_for_key);

/**
 * keyslot_manager_get_slot() - Increment the refcount on the specified slot.
 * @ksm - The keyslot manager that we want to modify.
 * @slot - The slot to increment the refcount of.
 *
 * This function assumes that there is already an active reference to that slot
 * and simply increments the refcount. This is useful when cloning a bio that
 * already has a reference to a keyslot, and we want the cloned bio to also have
 * its own reference.
 *
 * Context: Any context.
 */
void keyslot_manager_get_slot(struct keyslot_manager *ksm, unsigned int slot)
{
	if (WARN_ON(slot >= ksm->num_slots))
		return;

	WARN_ON(atomic_inc_return(&ksm->slot_refs[slot]) < 2);
}
EXPORT_SYMBOL(keyslot_manager_get_slot);

/**
 * keyslot_manager_put_slot() - Release a reference to a slot
 * @ksm: The keyslot manager to release the reference from.
 * @slot: The slot to release the reference from.
 *
 * Context: Any context.
 */
void keyslot_manager_put_slot(struct keyslot_manager *ksm, unsigned int slot)
{
	if (WARN_ON(slot >= ksm->num_slots))
		return;

	if (atomic_dec_and_test(&ksm->slot_refs[slot])) {
		atomic_inc(&ksm->num_idle_slots);
		wake_up(&ksm->wait_queue);
	}
}
EXPORT_SYMBOL(keyslot_manager_put_slot);

/**
 * keyslot_manager_evict_key() - Evict a key from the lower layer device.
 * @ksm - The keyslot manager to evict from
 * @key - The key to evict
 * @crypt_mode - The crypto algorithm the key was programmed with.
 * @data_unit_size - The data_unit_size the key was programmed with.
 *
 * Finds the slot that the specified key, crypt_mode, data_unit_size combo
 * was programmed into, and evicts that slot from the lower layer device if
 * the refcount on the slot is 0. Returns -EBUSY if the refcount is not 0, and
 * -errno on error.
 *
 * Context: Process context. Takes and releases ksm->lock.
 */
int keyslot_manager_evict_key(struct keyslot_manager *ksm,
			      const u8 *key,
			      enum blk_crypt_mode_num crypt_mode,
			      unsigned int data_unit_size)
{
	int slot;
	int err = 0;

	if (ksm->num_slots == 0) {
		return ksm->ksm_ll_ops.keyslot_evict(ksm->ll_priv_data, key,
						     crypt_mode,
						     data_unit_size, -1);
	}

	mutex_lock(&ksm->lock);
	slot = ksm->ksm_ll_ops.keyslot_find(ksm->ll_priv_data, key,
					    crypt_mode,
					    data_unit_size);

	if (slot < 0) {
		mutex_unlock(&ksm->lock);
		return slot;
	}

	if (atomic_read(&ksm->slot_refs[slot]) == 0) {
		err = ksm->ksm_ll_ops.keyslot_evict(ksm->ll_priv_data, key,
						    crypt_mode,
						    data_unit_size,
						    slot);
	} else {
		err = -EBUSY;
	}

	mutex_unlock(&ksm->lock);
	return err;
}
EXPORT_SYMBOL(keyslot_manager_evict_key);

void keyslot_manager_destroy(struct keyslot_manager *ksm)
{
	if (!ksm)
		return;

	if (ksm->last_used_seq_nums)
		kzfree(ksm->last_used_seq_nums);

	kzfree(ksm);
}
EXPORT_SYMBOL(keyslot_manager_destroy);

/**
 * keyslot_manager_create_passthrough() - Create a passthrough keyslot manager
 * @ksm_ll_ops: The struct keyslot_mgmt_ll_ops for the device that this keyslot
 *		manager will use to perform operations like programming and
 *		evicting keys.
 * @ll_priv_data: Private data passed as is to the functions in ksm_ll_ops.
 *
 * Allocate memory for and initialize a keyslot manager. Called by for e.g.
 * storage drivers to set up a keyslot manager in their request_queue.
 *
 * Context: This function may sleep
 * Return: Pointer to constructed keyslot manager or NULL on error.
 */
struct keyslot_manager *keyslot_manager_create_passthrough(
			const struct keyslot_mgmt_ll_ops *ksm_ll_ops,
			void *ll_priv_data)
{
	struct keyslot_manager *ksm;

	/* Check that op for evict is specified */
	if (ksm_ll_ops->keyslot_evict == NULL) {
		return NULL;
	}

	ksm = kzalloc(sizeof(*ksm), GFP_KERNEL);
	if (!ksm)
		return NULL;

	ksm->num_slots = 0;
	return ksm;
}
EXPORT_SYMBOL(keyslot_manager_create_passthrough);

bool keyslot_manager_is_passthrough(struct keyslot_manager *ksm)
{
	return ksm->num_slots == 0;
}
EXPORT_SYMBOL(keyslot_manager_is_passthrough);
