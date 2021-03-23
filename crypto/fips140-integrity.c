// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google Inc
 * Author: Ard Biesheuvel <ardb@google.com>
 */

#define pr_fmt(fmt) "fips140: " fmt

#include <linux/ctype.h>
#include <linux/module.h>
#include <crypto/aead.h>
#include <crypto/aes.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <crypto/skcipher.h>
#include <crypto/rng.h>
#include <trace/hooks/libaes.h>
#include <trace/hooks/libsha256.h>

#include "internal.h"

u8 __initdata fips140_integ_hmac_key[] = "The quick brown fox jumps over the lazy dog";

/* this is populated by the build tool */
u8 __initdata fips140_integ_hmac_digest[SHA256_DIGEST_SIZE];

const u32 __initcall_start_marker __section(".initcalls._start");
const u32 __initcall_end_marker __section(".initcalls._end");

const u8 __fips140_text_start __section(".text.._start");
const u8 __fips140_text_end __section(".text.._end");

const u8 __fips140_rodata_start __section(".rodata.._start");
const u8 __fips140_rodata_end __section(".rodata.._end");

/*
 * We need this little detour to prevent Clang from detecting out of bounds
 * accesses to __fips140_text_start and __fips140_rodata_start, which only exist
 * to delineate the section, and so their sizes are not relevent to us.
 */
const u8 *__text_start = &__fips140_text_start;
const u8 *__rodata_start = &__fips140_rodata_start;

static const char fips140_ciphers[][22] __initconst = {
	"aes",

	"gcm(aes)",

	"ecb(aes)",
	"cbc(aes)",
	"ctr(aes)",
	"xts(aes)",

	"hmac(sha1)",
	"hmac(sha224)",
	"hmac(sha256)",
	"hmac(sha384)",
	"hmac(sha512)",
	"sha1",
	"sha224",
	"sha256",
	"sha384",
	"sha512",

	"drbg_nopr_ctr_aes256",
	"drbg_nopr_ctr_aes192",
	"drbg_nopr_ctr_aes128",
	"drbg_nopr_hmac_sha512",
	"drbg_nopr_hmac_sha384",
	"drbg_nopr_hmac_sha256",
	"drbg_nopr_hmac_sha1",
	"drbg_nopr_sha512",
	"drbg_nopr_sha384",
	"drbg_nopr_sha256",
	"drbg_nopr_sha1",
	"drbg_pr_ctr_aes256",
	"drbg_pr_ctr_aes192",
	"drbg_pr_ctr_aes128",
	"drbg_pr_hmac_sha512",
	"drbg_pr_hmac_sha384",
	"drbg_pr_hmac_sha256",
	"drbg_pr_hmac_sha1",
	"drbg_pr_sha512",
	"drbg_pr_sha384",
	"drbg_pr_sha256",
	"drbg_pr_sha1",
};

static bool __init is_fips140_algo(struct crypto_alg *alg)
{
	int i;

	/*
	 * All software algorithms are synchronous, hardware algorithms must
	 * be covered by their own FIPS 140-2 certification.
	 */
	if (alg->cra_flags & CRYPTO_ALG_ASYNC)
		return false;

	for (i = 0; i < ARRAY_SIZE(fips140_ciphers); i++)
		if (!strcmp(alg->cra_name, fips140_ciphers[i]))
			return true;
	return false;
}

static void __init unregister_existing_fips140_algos(void)
{
	struct crypto_alg *alg;

	down_read(&crypto_alg_sem);

	/*
	 * Find all registered algorithms that we care about, and disable them
	 * if they are not in active use. If they are, we cannot simply disable
	 * them but we can adapt them later to use our integrity checked code.
	 */
	list_for_each_entry(alg, &crypto_alg_list, cra_list) {
		struct crypto_instance *inst;
		char *s;

		if (!is_fips140_algo(alg))
			continue;

		if (refcount_read(&alg->cra_refcnt) == 1)
			alg->cra_flags |= CRYPTO_ALG_DYING;
		else
			/*
			 * Mark this algo as needing further handling, by
			 * setting the priority to a negative value (which
			 * never occurs otherwise)
			 */
			alg->cra_priority = -1;

		/*
		 * If this algo was instantiated from a template, find the
		 * template and disable it by changing its name to all caps.
		 * We may visit the same template several times, but that is
		 * fine.
		 */
		if (alg->cra_flags & CRYPTO_ALG_INSTANCE) {
			inst = container_of(alg, struct crypto_instance, alg);
			for (s = inst->tmpl->name; *s != '\0'; s++)
				*s = toupper(*s);
		}
	}

	up_read(&crypto_alg_sem);
}

static void __init unapply_text_relocations(void *section, int section_size,
					    const Elf64_Rela *rela, int numrels)
{
	while (numrels--) {
		u32 *place = (u32 *)(section + rela->r_offset);

		BUG_ON(rela->r_offset >= section_size);

		switch (ELF64_R_TYPE(rela->r_info)) {
#ifdef CONFIG_ARM64
		case R_AARCH64_JUMP26:
		case R_AARCH64_CALL26:
			*place &= ~GENMASK(25, 0);
			break;

		case R_AARCH64_ADR_PREL_LO21:
		case R_AARCH64_ADR_PREL_PG_HI21:
		case R_AARCH64_ADR_PREL_PG_HI21_NC:
			*place &= ~(GENMASK(30, 29) | GENMASK(23, 5));
			break;

		case R_AARCH64_ADD_ABS_LO12_NC:
		case R_AARCH64_LDST8_ABS_LO12_NC:
		case R_AARCH64_LDST16_ABS_LO12_NC:
		case R_AARCH64_LDST32_ABS_LO12_NC:
		case R_AARCH64_LDST64_ABS_LO12_NC:
		case R_AARCH64_LDST128_ABS_LO12_NC:
			*place &= ~GENMASK(21, 10);
			break;
		default:
			pr_err("unhandled relocation type %d\n",
			       ELF64_R_TYPE(rela->r_info));
			BUG();
#else
#error
#endif
		}
		rela++;
	}
}

static void __init unapply_rodata_relocations(void *section, int section_size,
					      const Elf64_Rela *rela, int numrels)
{
	while (numrels--) {
		void *place = section + rela->r_offset;

		BUG_ON(rela->r_offset >= section_size);

		switch (ELF64_R_TYPE(rela->r_info)) {
#ifdef CONFIG_ARM64
		case R_AARCH64_ABS64:
			*(u64 *)place = 0;
			break;
		default:
			pr_err("unhandled relocation type %d\n",
			       ELF64_R_TYPE(rela->r_info));
			BUG();
#else
#error
#endif
		}
		rela++;
	}
}

static bool __init check_fips140_module_hmac(void)
{
	SHASH_DESC_ON_STACK(desc, dontcare);
	u8 digest[SHA256_DIGEST_SIZE];
	void *textcopy, *rodatacopy;
	int textsize, rodatasize;
	int err;

	textsize	= &__fips140_text_end - &__fips140_text_start;
	rodatasize	= &__fips140_rodata_end - &__fips140_rodata_start;

	pr_warn("text size  : 0x%x\n", textsize);
	pr_warn("rodata size: 0x%x\n", rodatasize);

	textcopy = kmalloc(textsize + rodatasize, GFP_KERNEL);
	if (!textcopy) {
		pr_err("Failed to allocate memory for copy of .text\n");
		return false;
	}

	rodatacopy = textcopy + textsize;

	memcpy(textcopy, __text_start, textsize);
	memcpy(rodatacopy, __rodata_start, rodatasize);

	// apply the relocations in reverse on the copies of .text  and .rodata
	unapply_text_relocations(textcopy, textsize,
				 __this_module.arch.text_relocations,
				 __this_module.arch.num_text_relocations);

	unapply_rodata_relocations(rodatacopy, rodatasize,
				   __this_module.arch.rodata_relocations,
				   __this_module.arch.num_rodata_relocations);

	kfree(__this_module.arch.text_relocations);
	kfree(__this_module.arch.rodata_relocations);

	desc->tfm = crypto_alloc_shash("hmac(sha256)", 0, 0);
	if (IS_ERR(desc->tfm)) {
		pr_err("failed to allocate hmac tfm (%d)\n", PTR_ERR(desc->tfm));
		kfree(textcopy);
		return false;
	}

	pr_warn("using '%s' for integrity check\n",
		crypto_tfm_alg_driver_name(&desc->tfm->base));

	err = crypto_shash_setkey(desc->tfm, fips140_integ_hmac_key,
				  strlen(fips140_integ_hmac_key)) ?:
	      crypto_shash_init(desc) ?:
	      crypto_shash_update(desc, textcopy, textsize) ?:
	      crypto_shash_finup(desc, rodatacopy, rodatasize, digest);

	crypto_free_shash(desc->tfm);
	kfree(textcopy);

	if (err) {
		pr_err("failed to calculate hmac shash (%d)\n", err);
		return false;
	}

	if (memcmp(digest, fips140_integ_hmac_digest, sizeof(digest))) {
		int i;

		pr_err("provided digest  :");
		for (i = 0; i < sizeof(digest); i++)
			pr_cont(" %02x", fips140_integ_hmac_digest[i]);
		pr_cont("\n");

		pr_err("calculated digest:");
		for (i = 0; i < sizeof(digest); i++)
			pr_cont(" %02x", digest[i]);
		pr_cont("\n");

		return false;
	}

	return true;
}

static bool __init update_live_fips140_algos(void)
{
	struct crypto_alg *alg, *new_alg;

	down_write(&crypto_alg_sem);

	/*
	 * Find all algorithms that we could not unregister the last time
	 * around, due to the fact that they were already in use.
	 */
	list_for_each_entry(alg, &crypto_alg_list, cra_list) {
		if (alg->cra_priority != -1 || !is_fips140_algo(alg))
			continue;

		/* grab the algo that will replace the live one */
		new_alg = crypto_alg_mod_lookup(alg->cra_driver_name,
						alg->cra_flags & CRYPTO_ALG_TYPE_MASK,
						CRYPTO_ALG_TYPE_MASK | CRYPTO_NOLOAD);

		if (!new_alg) {
			pr_crit("Failed to allocate '%s' for updating live algo\n",
				alg->cra_driver_name);
			return false;
		}

		// TODO how to deal with template based algos

		switch (alg->cra_flags & CRYPTO_ALG_TYPE_MASK) {
			struct aead_alg *old_aead, *new_aead;
			struct skcipher_alg *old_skcipher, *new_skcipher;
			struct shash_alg *old_shash, *new_shash;
			struct rng_alg *old_rng, *new_rng;

			case CRYPTO_ALG_TYPE_CIPHER:
				alg->cra_u.cipher = new_alg->cra_u.cipher;
				break;

			case CRYPTO_ALG_TYPE_AEAD:
				old_aead = container_of(alg, struct aead_alg, base);
				new_aead = container_of(new_alg, struct aead_alg, base);

				old_aead->setkey	= new_aead->setkey;
				old_aead->setauthsize	= new_aead->setauthsize;
				old_aead->encrypt	= new_aead->encrypt;
				old_aead->decrypt	= new_aead->decrypt;
				old_aead->init		= new_aead->init;
				old_aead->exit		= new_aead->exit;
				break;

			case CRYPTO_ALG_TYPE_SKCIPHER:
				old_skcipher = container_of(alg, struct skcipher_alg, base);
				new_skcipher = container_of(new_alg, struct skcipher_alg, base);

				old_skcipher->setkey	= new_skcipher->setkey;
				old_skcipher->encrypt	= new_skcipher->encrypt;
				old_skcipher->decrypt	= new_skcipher->decrypt;
				old_skcipher->init	= new_skcipher->init;
				old_skcipher->exit	= new_skcipher->exit;
				break;

			case CRYPTO_ALG_TYPE_SHASH:
				old_shash = container_of(alg, struct shash_alg, base);
				new_shash = container_of(new_alg, struct shash_alg, base);

				old_shash->init		= new_shash->init;
				old_shash->update	= new_shash->update;
				old_shash->final	= new_shash->final;
				old_shash->finup	= new_shash->finup;
				old_shash->digest	= new_shash->digest;
				old_shash->export	= new_shash->export;
				old_shash->import	= new_shash->import;
				old_shash->setkey	= new_shash->setkey;
				old_shash->init_tfm	= new_shash->init_tfm;
				old_shash->exit_tfm	= new_shash->exit_tfm;
				break;

			case CRYPTO_ALG_TYPE_RNG:
				old_rng = container_of(alg, struct rng_alg, base);
				new_rng = container_of(new_alg, struct rng_alg, base);

				old_rng->generate	= new_rng->generate;
				old_rng->seed		= new_rng->seed;
				old_rng->set_ent	= new_rng->set_ent;
				break;
		}
	}
	up_write(&crypto_alg_sem);

	return true;
}

static void fips140_sha256(void *p, const u8 *data, unsigned int *len, u8 *out)
{
	sha256(data, *len, out);
	*len = -1;
}

static void fips140_aes_expandkey(void *p, struct crypto_aes_ctx *ctx,
				  const u8 *in_key, unsigned int key_len,
				  int *err)
{
	*err = aes_expandkey(ctx, in_key, key_len);
}

static void fips140_aes_encrypt(void *priv, const struct crypto_aes_ctx *ctx,
				u8 *out, const u8 *in, int *ret)
{
	aes_encrypt(ctx, out, in);
	*ret = 1;
}

static void fips140_aes_decrypt(void *priv, const struct crypto_aes_ctx *ctx,
				u8 *out, const u8 *in, int *ret)
{
	aes_decrypt(ctx, out, in);
	*ret = 1;
}

static bool update_fips140_library_routines(void)
{
	int ret;

	ret = register_trace_android_vh_sha256(fips140_sha256, NULL) ?:
	      register_trace_android_vh_aes_expandkey(fips140_aes_expandkey, NULL) ?:
	      register_trace_android_vh_aes_encrypt(fips140_aes_encrypt, NULL) ?:
	      register_trace_android_vh_aes_decrypt(fips140_aes_decrypt, NULL);

	return ret == 0;
}


int __init __attribute__((__no_sanitize__("cfi"))) fips140_init(void)
{
	const u32 *initcall;

	unregister_existing_fips140_algos();

	/* iterate over all init routines present in this module and call them */
	for (initcall = &__initcall_start_marker + 1;
	     initcall < &__initcall_end_marker;
	     initcall++) {
		int (*init)(void) = offset_to_ptr(initcall);

		init();
	}

	if (!update_live_fips140_algos())
		goto panic;

	if (!update_fips140_library_routines())
		goto panic;

	/*
	 * Wait until all tasks have at least been scheduled once and preempted
	 * voluntarily. This ensures that none of the superseded algorithms that
	 * were already in use will still be live.
	 */
	synchronize_rcu_tasks();

	/* insert self tests here */

	if (!check_fips140_module_hmac()) {
		pr_crit("FIPS 140-2 integrity check failed -- giving up!\n");
		goto panic;
	}

	pr_warn("integrity check successful\n");
	return 0;

panic:
	panic("FIPS 140-2 module load failure");
}

module_init(fips140_init);

MODULE_IMPORT_NS(CRYPTO_INTERNAL);
MODULE_LICENSE("GPL v2");

/*
 * Crypto library routines that are reproduced here so they will be covered
 * by the FIPS 140-2 integrity check.
 */
void __crypto_xor(u8 *dst, const u8 *src1, const u8 *src2, unsigned int len)
{
	while (len >= 8) {
		*(u64 *)dst = *(u64 *)src1 ^  *(u64 *)src2;
		dst += 8;
		src1 += 8;
		src2 += 8;
		len -= 8;
	}

	while (len >= 4) {
		*(u32 *)dst = *(u32 *)src1 ^ *(u32 *)src2;
		dst += 4;
		src1 += 4;
		src2 += 4;
		len -= 4;
	}

	while (len >= 2) {
		*(u16 *)dst = *(u16 *)src1 ^ *(u16 *)src2;
		dst += 2;
		src1 += 2;
		src2 += 2;
		len -= 2;
	}

	while (len--)
		*dst++ = *src1++ ^ *src2++;
}

void crypto_inc(u8 *a, unsigned int size)
{
	a += size;

	while (size--)
		if (++*--a)
			break;
}
