// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is an implementation of the BLAKE2s hash and PRF functions.
 *
 * Information: https://blake2.net/
 *
 */

#include <crypto/internal/blake2s.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bug.h>
<<<<<<< HEAD   (64f180 Merge branch 'android13-5.10' into branch 'android13-5.10-lt)

#if IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_BLAKE2S)
#  define blake2s_compress blake2s_compress_arch
#else
#  define blake2s_compress blake2s_compress_generic
#endif
=======
>>>>>>> BRANCH (56c31a Linux 5.10.119)

void blake2s_update(struct blake2s_state *state, const u8 *in, size_t inlen)
{
<<<<<<< HEAD   (64f180 Merge branch 'android13-5.10' into branch 'android13-5.10-lt)
	__blake2s_update(state, in, inlen, blake2s_compress);
=======
	__blake2s_update(state, in, inlen, false);
>>>>>>> BRANCH (56c31a Linux 5.10.119)
}
EXPORT_SYMBOL(blake2s_update);

void blake2s_final(struct blake2s_state *state, u8 *out)
{
	WARN_ON(IS_ENABLED(DEBUG) && !out);
<<<<<<< HEAD   (64f180 Merge branch 'android13-5.10' into branch 'android13-5.10-lt)
	__blake2s_final(state, out, blake2s_compress);
=======
	__blake2s_final(state, out, false);
>>>>>>> BRANCH (56c31a Linux 5.10.119)
	memzero_explicit(state, sizeof(*state));
}
EXPORT_SYMBOL(blake2s_final);

static int __init mod_init(void)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_MANAGER_DISABLE_TESTS) &&
	    WARN_ON(!blake2s_selftest()))
		return -ENODEV;
	return 0;
}

static void __exit mod_exit(void)
{
}

module_init(mod_init);
module_exit(mod_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BLAKE2s hash function");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
