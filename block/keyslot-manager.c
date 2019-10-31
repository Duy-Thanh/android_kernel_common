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
#include <crypto/algapi.h>
#include <linux/keyslot-manager.h>
#include <linux/atomic.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/blkdev.h>

struct keyslot {
	atomic_t slot_refs;
	struct list_head idle_slot_node;
	struct hlist_node hash_node;
	struct blk_crypto_key key;
};

struct keyslot_manager {
	unsigned int num_slots;
	atomic_t num_idle_slots;
	struct keyslot_mgmt_ll_ops ksm_ll_ops;
	void *ll_priv_data;

	/* Protects programming and evicting keys from the device */
	struct rw_semaphore lock;

	/* List of idle slots, with least recently used slot at front */
	wait_queue_head_t idle_slots_wait_queue;
	struct list_head idle_slots;
	spinlock_t idle_slots_lock;

	/*
	 * Hash table which maps key hashes to keyslots, so that we can find a
	 * key's keyslot in O(1) time rather than O(num_slots).  Protected by
	 * 'lock'.  A cryptographic hash function is used so that timing attacks
	 * can't leak information about the raw keys.
	 */
	struct hlist_head *slot_hashtable;
	unsigned int slot_hashtable_size;

	/* Per-keyslot data */
	struct keyslot slots[];
};

/**
 * keyslot_manager_create() - Create a keyslot manager
 * @num_slots: The number of key slots to manage.
 * @ksm_ll_ops: The struct keyslot_mgmt_ll_ops for the device that this keyslot
 *		manager will use to perform operations like programming and
 *		evicting keys.
 * @ll_priv_data: Private data passed as is to the functions in ksm_ll_ops.
 *
 * Allocate memory for and initialize a keyslot manager. Called by e.g.
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
	unsigned int slot;
	unsigned int i;

	if (num_slots == 0)
		return NULL;

	/* Check that all ops are specified */
	if (ksm_ll_ops->keyslot_program == NULL ||
	    ksm_ll_ops->keyslot_evict == NULL ||
	    ksm_ll_ops->crypto_mode_supported == NULL)
		return NULL;

	ksm = kvzalloc(struct_size(ksm, slots, num_slots), GFP_KERNEL);
	if (!ksm)
		return NULL;

	ksm->num_slots = num_slots;
	atomic_set(&ksm->num_idle_slots, num_slots);
	ksm->ksm_ll_ops = *ksm_ll_ops;
	ksm->ll_priv_data = ll_priv_data;

	init_rwsem(&ksm->lock);

	init_waitqueue_head(&ksm->idle_slots_wait_queue);
	INIT_LIST_HEAD(&ksm->idle_slots);

	for (slot = 0; slot < num_slots; slot++) {
		list_add_tail(&ksm->slots[slot].idle_slot_node,
			      &ksm->idle_slots);
	}

	spin_lock_init(&ksm->idle_slots_lock);

	ksm->slot_hashtable_size = roundup_pow_of_two(num_slots);
	ksm->slot_hashtable = kvmalloc_array(ksm->slot_hashtable_size,
					     sizeof(ksm->slot_hashtable[0]),
					     GFP_KERNEL);
	if (!ksm->slot_hashtable)
		goto err_free_ksm;
	for (i = 0; i < ksm->slot_hashtable_size; i++)
		INIT_HLIST_HEAD(&ksm->slot_hashtable[i]);

	return ksm;

err_free_ksm:
	keyslot_manager_destroy(ksm);
	return NULL;
}
EXPORT_SYMBOL(keyslot_manager_create);

static inline struct hlist_head *
hash_bucket_for_key(struct keyslot_manager *ksm,
		    const struct blk_crypto_key *key)
{
	return &ksm->slot_hashtable[key->hash & (ksm->slot_hashtable_size - 1)];
}

static void remove_slot_from_lru_list(struct keyslot_manager *ksm, int slot)
{
	unsigned long flags;

	spin_lock_irqsave(&ksm->idle_slots_lock, flags);
	list_del(&ksm->slots[slot].idle_slot_node);
	spin_unlock_irqrestore(&ksm->idle_slots_lock, flags);

	atomic_dec(&ksm->num_idle_slots);
}

static int find_keyslot(struct keyslot_manager *ksm,
			const struct blk_crypto_key *key)
{
	const struct hlist_head *head = hash_bucket_for_key(ksm, key);
	const struct keyslot *slotp;

	hlist_for_each_entry(slotp, head, hash_node) {
		if (slotp->key.hash == key->hash &&
		    slotp->key.crypto_mode == key->crypto_mode &&
		    slotp->key.data_unit_size == key->data_unit_size &&
		    !crypto_memneq(slotp->key.raw, key->raw, key->size))
			return slotp - ksm->slots;
	}
	return -ENOKEY;
}

static int find_and_grab_keyslot(struct keyslot_manager *ksm,
				 const struct blk_crypto_key *key)
{
	int slot;

	slot = find_keyslot(ksm, key);
	if (slot < 0)
		return slot;
	if (atomic_inc_return(&ksm->slots[slot].slot_refs) == 1) {
		/* Took first reference to this slot; remove it from LRU list */
		remove_slot_from_lru_list(ksm, slot);
	}
	return slot;
}

/**
 * keyslot_manager_get_slot_for_key() - Program a key into a keyslot.
 * @ksm: The keyslot manager to program the key into.
 * @key: Pointer to the key object to program, including the raw key, crypto
 *	 mode, and data unit size.
 *
 * Get a keyslot that's been programmed with the specified key.  If one already
 * exists, return it with incremented refcount.  Otherwise, wait for a keyslot
 * to become idle and program it.
 *
 * Context: Process context. Takes and releases ksm->lock.
 * Return: The keyslot on success, else a -errno value.
 */
int keyslot_manager_get_slot_for_key(struct keyslot_manager *ksm,
				     const struct blk_crypto_key *key)
{
	int slot;
	int err;
	struct keyslot *idle_slot;

	down_read(&ksm->lock);
	slot = find_and_grab_keyslot(ksm, key);
	up_read(&ksm->lock);
	if (slot != -ENOKEY)
		return slot;

	for (;;) {
		down_write(&ksm->lock);
		slot = find_and_grab_keyslot(ksm, key);
		if (slot != -ENOKEY) {
			up_write(&ksm->lock);
			return slot;
		}

		/*
		 * If we're here, that means there wasn't a slot that was
		 * already programmed with the key. So try to program it.
		 */
		if (atomic_read(&ksm->num_idle_slots) > 0)
			break;

		up_write(&ksm->lock);
		wait_event(ksm->idle_slots_wait_queue,
			(atomic_read(&ksm->num_idle_slots) > 0));
	}

	idle_slot = list_first_entry(&ksm->idle_slots, struct keyslot,
					     idle_slot_node);
	slot = idle_slot - ksm->slots;

	err = ksm->ksm_ll_ops.keyslot_program(ksm->ll_priv_data, key, slot);
	if (err) {
		wake_up(&ksm->idle_slots_wait_queue);
		up_write(&ksm->lock);
		return err;
	}

	/* Move this slot to the hash list for the new key. */
	if (idle_slot->key.crypto_mode != BLK_ENCRYPTION_MODE_INVALID)
		hlist_del(&idle_slot->hash_node);
	hlist_add_head(&idle_slot->hash_node, hash_bucket_for_key(ksm, key));

	atomic_set(&idle_slot->slot_refs, 1);
	idle_slot->key = *key;

	remove_slot_from_lru_list(ksm, slot);

	up_write(&ksm->lock);
	return slot;

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

	WARN_ON(atomic_inc_return(&ksm->slots[slot].slot_refs) < 2);
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
	unsigned long flags;

	if (WARN_ON(slot >= ksm->num_slots))
		return;

	if (atomic_dec_and_lock_irqsave(&ksm->slots[slot].slot_refs,
					&ksm->idle_slots_lock, flags)) {
		list_add_tail(&ksm->slots[slot].idle_slot_node,
			      &ksm->idle_slots);
		spin_unlock_irqrestore(&ksm->idle_slots_lock, flags);
		atomic_inc(&ksm->num_idle_slots);
		wake_up(&ksm->idle_slots_wait_queue);
	}
}
EXPORT_SYMBOL(keyslot_manager_put_slot);

/**
 * keyslot_manager_crypto_mode_supported() - Find out if a crypto_mode/data
 *					     unit size combination is supported
 *					     by a ksm.
 * @ksm - The keyslot manager to check
 * @crypto_mode - The crypto mode to check for.
 * @data_unit_size - The data_unit_size for the mode.
 *
 * Calls and returns the result of the crypto_mode_supported function specified
 * by the ksm.
 *
 * Context: Process context.
 * Return: Whether or not this ksm supports the specified crypto_mode/
 *	   data_unit_size combo.
 */
bool keyslot_manager_crypto_mode_supported(struct keyslot_manager *ksm,
					   enum blk_crypto_mode_num crypto_mode,
					   unsigned int data_unit_size)
{
	if (!ksm)
		return false;
	return ksm->ksm_ll_ops.crypto_mode_supported(ksm->ll_priv_data,
						     crypto_mode,
						     data_unit_size);
}
EXPORT_SYMBOL(keyslot_manager_crypto_mode_supported);

bool keyslot_manager_rq_crypto_mode_supported(struct request_queue *q,
					enum blk_crypto_mode_num crypto_mode,
					unsigned int data_unit_size)
{
	return keyslot_manager_crypto_mode_supported(q->ksm, crypto_mode,
						     data_unit_size);
}
EXPORT_SYMBOL(keyslot_manager_rq_crypto_mode_supported);

/**
 * keyslot_manager_evict_key() - Evict a key from the lower layer device.
 * @ksm - The keyslot manager to evict from
 * @key - The key to evict
 *
 * Find the keyslot that the specified key was programmed into, and evict that
 * slot from the lower layer device if that slot is not currently in use.
 *
 * Context: Process context. Takes and releases ksm->lock.
 * Return: 0 on success, -EBUSY if the keyslot is still in use, or another
 *	   -errno value on other error.
 */
int keyslot_manager_evict_key(struct keyslot_manager *ksm,
			      const struct blk_crypto_key *key)
{
	int slot;
	int err;
	struct keyslot *slotp;

	down_write(&ksm->lock);
	slot = find_keyslot(ksm, key);
	if (slot < 0) {
		err = slot;
		goto out_unlock;
	}
	slotp = &ksm->slots[slot];

	if (atomic_read(&slotp->slot_refs) != 0) {
		err = -EBUSY;
		goto out_unlock;
	}
	err = ksm->ksm_ll_ops.keyslot_evict(ksm->ll_priv_data, key, slot);
	if (err)
		goto out_unlock;

	hlist_del(&slotp->hash_node);
	BUILD_BUG_ON(BLK_ENCRYPTION_MODE_INVALID != 0);
	memzero_explicit(&slotp->key, sizeof(slotp->key));
	err = 0;
out_unlock:
	up_write(&ksm->lock);
	return err;
}
EXPORT_SYMBOL(keyslot_manager_evict_key);

/**
 * keyslot_manager_reprogram_all_keys() - Re-program all keyslots.
 * @ksm: The keyslot manager
 *
 * Re-program all keyslots that are supposed to have a key programmed.  This is
 * intended only for use by drivers for hardware that loses its keys on reset.
 *
 * Context: Process context. Takes and releases ksm->lock.
 */
void keyslot_manager_reprogram_all_keys(struct keyslot_manager *ksm)
{
	unsigned int slot;

	down_write(&ksm->lock);
	for (slot = 0; slot < ksm->num_slots; slot++) {
		const struct keyslot *slotp = &ksm->slots[slot];
		int err;

		if (slotp->key.crypto_mode == BLK_ENCRYPTION_MODE_INVALID)
			continue;

		err = ksm->ksm_ll_ops.keyslot_program(ksm->ll_priv_data,
						      &slotp->key, slot);
		WARN_ON(err);
	}
	up_write(&ksm->lock);
}
EXPORT_SYMBOL(keyslot_manager_reprogram_all_keys);

void keyslot_manager_destroy(struct keyslot_manager *ksm)
{
	if (ksm) {
		kvfree(ksm->slot_hashtable);
		memzero_explicit(ksm, struct_size(ksm, slots, ksm->num_slots));
		kvfree(ksm);
	}
}
EXPORT_SYMBOL(keyslot_manager_destroy);
