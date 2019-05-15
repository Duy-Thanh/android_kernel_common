/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#include <linux/types.h>
#include <linux/blk_types.h>

#ifndef __LINUX_KEYSLOT_MANAGER_H
#define __LINUX_KEYSLOT_MANAGER_H

/**
 * struct keyslot_mgmt_ll_ops - functions to manage keyslots in hardware
 * @keyslot_program:	Program the specified key and algorithm into the
 *			specified slot in the inline encryption hardware.
 * @keyslot_evict:	Evict key from the specified keyslot in the hardware.
 *			The key, crypt_mode and data_unit_size are also passed
 *			down so that for e.g. dm layers can evict keys from
 *			the devices that they map over.
 *			Returns 0 on success, -errno otherwise.
 * @crypt_mode_supported:	Check whether a crypt_mode and data_unit_size
 *				combo is supported.
 * @keyslot_find:	Returns the slot number that matches the key,
 *			or -ENOKEY if no match found, or -errno on
 *			error.
 *
 * This structure should be provided by storage device drivers when they set up
 * a keyslot manager - this structure holds the function ptrs that the keyslot
 * manager will use to manipulate keyslots in the hardware.
 */
struct keyslot_mgmt_ll_ops {
	int (*keyslot_program)(void *ll_priv_data, const u8 *key,
			       enum blk_crypt_mode_num crypt_mode,
			       unsigned int data_unit_size,
			       unsigned int slot);
	int (*keyslot_evict)(void *ll_priv_data, const u8 *key,
			     enum blk_crypt_mode_num crypt_mode,
			     unsigned int data_unit_size,
			     unsigned int slot);
	bool (*crypt_mode_supported)(void *ll_priv_data,
				      enum blk_crypt_mode_num crypt_mode,
				      unsigned int data_unit_size);
	int (*keyslot_find)(void *ll_priv_data, const u8 *key,
			    enum blk_crypt_mode_num crypt_mode,
			    unsigned int data_unit_size);
};

#ifdef CONFIG_BLK_INLINE_ENCRYPTION
struct keyslot_manager;

extern struct keyslot_manager *keyslot_manager_create(unsigned int num_slots,
				const struct keyslot_mgmt_ll_ops *ksm_ops,
				void *ll_priv_data);

extern int
keyslot_manager_get_slot_for_key(struct keyslot_manager *ksm,
				 const u8 *key,
				 enum blk_crypt_mode_num crypt_mode,
				 unsigned int data_unit_size);

extern void keyslot_manager_get_slot(struct keyslot_manager *ksm,
				     unsigned int slot);

extern void keyslot_manager_put_slot(struct keyslot_manager *ksm,
				     unsigned int slot);

extern int keyslot_manager_evict_key(struct keyslot_manager *ksm,
				     const u8 *key,
				     enum blk_crypt_mode_num crypt_mode,
				     unsigned int data_unit_size);

extern void keyslot_manager_destroy(struct keyslot_manager *ksm);

extern struct keyslot_manager *keyslot_manager_create_passthrough(
				const struct keyslot_mgmt_ll_ops *ksm_ops,
				void *ll_priv_data);

extern bool keyslot_manager_is_passthrough(struct keyslot_manager *ksm);

#else /* CONFIG_BLK_INLINE_ENCRYPTION */
struct keyslot_manager {};

static inline struct keyslot_manager *
keyslot_manager_create(unsigned int num_slots,
		       const struct keyslot_mgmt_ll_ops *ksm_ops,
		       void *ll_priv_data)
{
	return NULL;
}

static inline int
keyslot_manager_get_slot_for_key(struct keyslot_manager *ksm,
				 const u8 *key,
				 enum blk_crypt_mode_num crypt_mode,
				 unsigned int data_unit_size)
{
	return -EOPNOTSUPP;
}

static inline void keyslot_manager_get_slot(struct keyslot_manager *ksm,
					    unsigned int slot) { }

static inline int keyslot_manager_put_slot(struct keyslot_manager *ksm,
					   unsigned int slot)
{
	return -EOPNOTSUPP;
}

static inline int keyslot_manager_evict_key(struct keyslot_manager *ksm,
				     const u8 *key,
				     enum blk_crypt_mode_num crypt_mode,
				     unsigned int data_unit_size)
{
	return -EOPNOTSUPP;
}

static inline void keyslot_manager_destroy(struct keyslot_manager *ksm)
{ }

static inline struct keyslot_manager *
keyslot_manager_create_passthrough(const struct keyslot_mgmt_ll_ops *ksm_ops,
				   void *ll_priv_data)
{
	return NULL;
}

static inline bool keyslot_manager_is_passthrough(struct keyslot_manager *ksm)
{
	return false;
}
#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

#endif /* __LINUX_KEYSLOT_MANAGER_H */
