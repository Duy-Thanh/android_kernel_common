// SPDX-License-Identifier: GPL-2.0

#include <linux/err.h>
#include <linux/export.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper __force void *rust_helper_ERR_PTR(long err)
{
	return ERR_PTR(err);
}

__rust_helper bool rust_helper_IS_ERR(__force const void *ptr)
{
	return IS_ERR(ptr);
}

__rust_helper long rust_helper_PTR_ERR(__force const void *ptr)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
__force void *rust_helper_ERR_PTR(long err)
{
	return ERR_PTR(err);
}

bool rust_helper_IS_ERR(__force const void *ptr)
{
	return IS_ERR(ptr);
}

long rust_helper_PTR_ERR(__force const void *ptr)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	return PTR_ERR(ptr);
}
