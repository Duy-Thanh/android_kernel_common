// SPDX-License-Identifier: GPL-2.0

/* C helper for page_range.rs to work around a CFI violation.
 *
 * Bindgen currently pretends that `enum lru_status` is the same as an integer.
 * This assumption is fine ABI-wise, but once you add CFI to the mix, it
 * triggers a CFI violation because `enum lru_status` gets a different CFI tag.
 *
 * This file contains a workaround until bindgen can be fixed.
 *
 * Copyright (C) 2024 Google LLC.
 */
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
#include "page_range_helper.h"
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
#include <linux/list_lru.h>
#include <linux/spinlock.h>
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)

unsigned int rust_shrink_free_page(struct list_head *item,
				   struct list_lru_one *list, spinlock_t *lock,
				   void *cb_arg);

enum lru_status
rust_shrink_free_page_wrap(struct list_head *item, struct list_lru_one *list,
			   spinlock_t *lock, void *cb_arg)
{
	return rust_shrink_free_page(item, list, lock, cb_arg);
}
