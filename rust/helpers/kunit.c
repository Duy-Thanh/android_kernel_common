// SPDX-License-Identifier: GPL-2.0

#include <kunit/test-bug.h>
#include <linux/export.h>

<<<<<<< HEAD   (bdc81b76cc3e91b2a805bff546c72244ef2504fd Merge android16-6.12 into android16-6.12-kminext)
__rust_helper struct kunit *rust_helper_kunit_get_current_test(void)
||||||| BASE   (648e13791289125f4447184856d1b211f32d5daf Android Common Kernel: Initial commit)
=======
struct kunit *rust_helper_kunit_get_current_test(void)
>>>>>>> CHANGE (efe70d86de873c3b4d76fdd8e6ef37ffa3a1c4c4 ANDROID: Initialize android16-6.12-2025-09)
{
	return kunit_get_current_test();
}
