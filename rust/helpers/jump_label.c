// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/jump_label.h>

#ifndef CONFIG_JUMP_LABEL
<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper int rust_helper_static_key_count(struct static_key *key)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
int rust_helper_static_key_count(struct static_key *key)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	return static_key_count(key);
}
#endif
