// SPDX-License-Identifier: GPL-2.0

#include <linux/uaccess.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper unsigned long
rust_helper_copy_from_user(void *to, const void __user *from, unsigned long n)
{
	return copy_from_user(to, from, n);
}

__rust_helper unsigned long
rust_helper_copy_to_user(void __user *to, const void *from, unsigned long n)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
unsigned long rust_helper_copy_from_user(void *to, const void __user *from,
					 unsigned long n)
{
	return copy_from_user(to, from, n);
}

unsigned long rust_helper_copy_to_user(void __user *to, const void *from,
				       unsigned long n)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	return copy_to_user(to, from, n);
}
