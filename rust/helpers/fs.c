// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2024 Google LLC.
 */

#include <linux/fs.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper struct file *rust_helper_get_file(struct file *f)
{
	return get_file(f);
}

__rust_helper loff_t rust_helper_i_size_read(const struct inode *inode)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
struct file *rust_helper_get_file(struct file *f)
{
	return get_file(f);
}

loff_t rust_helper_i_size_read(const struct inode *inode)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	return i_size_read(inode);
}
