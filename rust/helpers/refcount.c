// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/refcount.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper refcount_t rust_helper_REFCOUNT_INIT(int n)
{
	return (refcount_t)REFCOUNT_INIT(n);
}

__rust_helper void rust_helper_refcount_inc(refcount_t *r)
{
	refcount_inc(r);
}

__rust_helper bool rust_helper_refcount_dec_and_test(refcount_t *r)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
refcount_t rust_helper_REFCOUNT_INIT(int n)
{
	return (refcount_t)REFCOUNT_INIT(n);
}

void rust_helper_refcount_inc(refcount_t *r)
{
	refcount_inc(r);
}

bool rust_helper_refcount_dec_and_test(refcount_t *r)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	return refcount_dec_and_test(r);
}
