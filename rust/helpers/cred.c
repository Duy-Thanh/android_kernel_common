// SPDX-License-Identifier: GPL-2.0

#include <linux/cred.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper const struct cred *rust_helper_get_cred(const struct cred *cred)
{
	return get_cred(cred);
}

__rust_helper void rust_helper_put_cred(const struct cred *cred)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
const struct cred *rust_helper_get_cred(const struct cred *cred)
{
	return get_cred(cred);
}

void rust_helper_put_cred(const struct cred *cred)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	put_cred(cred);
}
