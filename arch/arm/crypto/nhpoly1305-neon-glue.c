// SPDX-License-Identifier: GPL-2.0
/*
 * NHPoly1305 - ε-almost-∆-universal hash function for Adiantum
 * (NEON accelerated version)
 *
 * Copyright 2018 Google LLC
 */

#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/internal/hash.h>
#include <crypto/nhpoly1305.h>
#include <linux/module.h>

asmlinkage void nh_neon(const u32 *key, const u8 *message, size_t message_len,
			u8 hash[NH_HASH_BYTES]);

static void _nh_neon(const u32 *key, const u8 *message, size_t message_len,
		     __le64 hash[NH_NUM_PASSES])
{
	nh_neon(key, message, message_len, (u8 *)hash);
}

static int nhpoly1305_neon_update(struct shash_desc *desc,
				  const u8 *src, unsigned int srclen)
{
	if (srclen < 64 || !may_use_simd())
		return crypto_nhpoly1305_update(desc, src, srclen);

	do {
		unsigned int n = min_t(unsigned int, srclen, PAGE_SIZE);

		kernel_neon_begin();
		crypto_nhpoly1305_update_helper(desc, src, n, _nh_neon);
		kernel_neon_end();
		src += n;
		srclen -= n;
	} while (srclen);
	return 0;
}

static struct shash_alg nhpoly1305_alg = {
	.digestsize	= POLY1305_DIGEST_SIZE,
	.init		= crypto_nhpoly1305_init,
	.update		= nhpoly1305_neon_update,
	.final		= crypto_nhpoly1305_final,
	.setkey		= crypto_nhpoly1305_setkey,
	.descsize	= sizeof(struct nhpoly1305_state),
	.base		= {
		.cra_name		= "nhpoly1305",
		.cra_driver_name	= "nhpoly1305-neon",
		.cra_priority		= 200,
		.cra_ctxsize		= sizeof(struct nhpoly1305_key),
		.cra_module		= THIS_MODULE,
	},
};

static int __init nhpoly1305_mod_init(void)
{
	if (!(elf_hwcap & HWCAP_NEON))
		return -ENODEV;

	return crypto_register_shash(&nhpoly1305_alg);
}

static void __exit nhpoly1305_mod_exit(void)
{
	crypto_unregister_shash(&nhpoly1305_alg);
}

module_init(nhpoly1305_mod_init);
module_exit(nhpoly1305_mod_exit);

MODULE_DESCRIPTION("NHPoly1305 ε-almost-∆-universal hash function (NEON-accelerated)");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Eric Biggers <ebiggers@google.com>");
MODULE_ALIAS_CRYPTO("nhpoly1305");
MODULE_ALIAS_CRYPTO("nhpoly1305-neon");
