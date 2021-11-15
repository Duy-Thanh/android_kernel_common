// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */

/*
 * Refer to Documentation/block/inline-encryption.rst for detailed explanation.
 */

#define pr_fmt(fmt) "blk-crypto: " fmt

<<<<<<< HEAD   (04f660 ANDROID: virtio: virtio_pvclock: initial driver impl)
#include <linux/blk-crypto.h>
#include <linux/blkdev.h>
#include <linux/keyslot-manager.h>
#include <linux/random.h>
#include <linux/siphash.h>

#include "blk-crypto-internal.h"

const struct blk_crypto_mode blk_crypto_modes[] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.cipher_str = "xts(aes)",
		.keysize = 64,
		.ivsize = 16,
	},
	[BLK_ENCRYPTION_MODE_AES_128_CBC_ESSIV] = {
		.cipher_str = "essiv(cbc(aes),sha256)",
		.keysize = 16,
		.ivsize = 16,
	},
	[BLK_ENCRYPTION_MODE_ADIANTUM] = {
		.cipher_str = "adiantum(xchacha12,aes)",
		.keysize = 32,
		.ivsize = 32,
	},
};

/* Check that all I/O segments are data unit aligned */
static int bio_crypt_check_alignment(struct bio *bio)
{
	const unsigned int data_unit_size =
				bio->bi_crypt_context->bc_key->data_unit_size;
	struct bvec_iter iter;
	struct bio_vec bv;

	bio_for_each_segment(bv, bio, iter) {
		if (!IS_ALIGNED(bv.bv_len | bv.bv_offset, data_unit_size))
			return -EIO;
	}
	return 0;
}

/**
 * blk_crypto_submit_bio - handle submitting bio for inline encryption
 *
 * @bio_ptr: pointer to original bio pointer
 *
 * If the bio doesn't have inline encryption enabled or the submitter already
 * specified a keyslot for the target device, do nothing.  Else, a raw key must
 * have been provided, so acquire a device keyslot for it if supported.  Else,
 * use the crypto API fallback.
 *
 * When the crypto API fallback is used for encryption, blk-crypto may choose to
 * split the bio into 2 - the first one that will continue to be processed and
 * the second one that will be resubmitted via generic_make_request.
 * A bounce bio will be allocated to encrypt the contents of the aforementioned
 * "first one", and *bio_ptr will be updated to this bounce bio.
 *
 * Return: 0 if bio submission should continue; nonzero if bio_endio() was
 *	   already called so bio submission should abort.
 */
int blk_crypto_submit_bio(struct bio **bio_ptr)
{
	struct bio *bio = *bio_ptr;
	struct request_queue *q;
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;
	int err;

	if (!bc || !bio_has_data(bio))
		return 0;

	/*
	 * When a read bio is marked for fallback decryption, its bi_iter is
	 * saved so that when we decrypt the bio later, we know what part of it
	 * was marked for fallback decryption (when the bio is passed down after
	 * blk_crypto_submit bio, it may be split or advanced so we cannot rely
	 * on the bi_iter while decrypting in blk_crypto_endio)
	 */
	if (bio_crypt_fallback_crypted(bc))
		return 0;

	err = bio_crypt_check_alignment(bio);
	if (err) {
		bio->bi_status = BLK_STS_IOERR;
		goto out;
	}

	q = bio->bi_disk->queue;

	if (bc->bc_ksm) {
		/* Key already programmed into device? */
		if (q->ksm == bc->bc_ksm)
			return 0;

		/* Nope, release the existing keyslot. */
		bio_crypt_ctx_release_keyslot(bc);
	}

	/* Get device keyslot if supported */
	if (keyslot_manager_crypto_mode_supported(q->ksm,
				bc->bc_key->crypto_mode,
				blk_crypto_key_dun_bytes(bc->bc_key),
				bc->bc_key->data_unit_size,
				bc->bc_key->is_hw_wrapped)) {
		err = bio_crypt_ctx_acquire_keyslot(bc, q->ksm);
		if (!err)
			return 0;

		pr_warn_once("Failed to acquire keyslot for %s (err=%d).  Falling back to crypto API.\n",
			     bio->bi_disk->disk_name, err);
	}

	/* Fallback to crypto API */
	err = blk_crypto_fallback_submit_bio(bio_ptr);
	if (err)
		goto out;

	return 0;
out:
	bio_endio(*bio_ptr);
	return err;
}

/**
 * blk_crypto_endio - clean up bio w.r.t inline encryption during bio_endio
 *
 * @bio: the bio to clean up
 *
 * If blk_crypto_submit_bio decided to fallback to crypto API for this bio,
 * we queue the bio for decryption into a workqueue and return false,
 * and call bio_endio(bio) at a later time (after the bio has been decrypted).
 *
 * If the bio is not to be decrypted by the crypto API, this function releases
 * the reference to the keyslot that blk_crypto_submit_bio got.
 *
 * Return: true if bio_endio should continue; false otherwise (bio_endio will
 * be called again when bio has been decrypted).
 */
bool blk_crypto_endio(struct bio *bio)
{
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;

	if (!bc)
		return true;

	if (bio_crypt_fallback_crypted(bc)) {
		/*
		 * The only bios who's crypto is handled by the blk-crypto
		 * fallback when they reach here are those with
		 * bio_data_dir(bio) == READ, since WRITE bios that are
		 * encrypted by the crypto API fallback are handled by
		 * blk_crypto_encrypt_endio().
		 */
		return !blk_crypto_queue_decrypt_bio(bio);
	}

	if (bc->bc_keyslot >= 0)
		bio_crypt_ctx_release_keyslot(bc);

	return true;
}

/**
 * blk_crypto_init_key() - Prepare a key for use with blk-crypto
 * @blk_key: Pointer to the blk_crypto_key to initialize.
 * @raw_key: Pointer to the raw key.
 * @raw_key_size: Size of raw key.  Must be at least the required size for the
 *                chosen @crypto_mode; see blk_crypto_modes[].  (It's allowed
 *                to be longer than the mode's actual key size, in order to
 *                support inline encryption hardware that accepts wrapped keys.
 *                @is_hw_wrapped has to be set for such keys)
 * @is_hw_wrapped: Denotes @raw_key is wrapped.
 * @crypto_mode: identifier for the encryption algorithm to use
 * @dun_bytes: number of bytes that will be used to specify the DUN when this
 *	       key is used
 * @data_unit_size: the data unit size to use for en/decryption
 *
 * Return: The blk_crypto_key that was prepared, or an ERR_PTR() on error.  When
 *	   done using the key, it must be freed with blk_crypto_free_key().
 */
int blk_crypto_init_key(struct blk_crypto_key *blk_key,
			const u8 *raw_key, unsigned int raw_key_size,
			bool is_hw_wrapped,
			enum blk_crypto_mode_num crypto_mode,
			unsigned int dun_bytes,
			unsigned int data_unit_size)
{
	const struct blk_crypto_mode *mode;
	static siphash_key_t hash_key;
	u32 hash;

	memset(blk_key, 0, sizeof(*blk_key));

	if (crypto_mode >= ARRAY_SIZE(blk_crypto_modes))
		return -EINVAL;

	BUILD_BUG_ON(BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE < BLK_CRYPTO_MAX_KEY_SIZE);

	mode = &blk_crypto_modes[crypto_mode];
	if (is_hw_wrapped) {
		if (raw_key_size < mode->keysize ||
		    raw_key_size > BLK_CRYPTO_MAX_WRAPPED_KEY_SIZE)
			return -EINVAL;
	} else {
		if (raw_key_size != mode->keysize)
			return -EINVAL;
	}

	if (dun_bytes <= 0 || dun_bytes > BLK_CRYPTO_MAX_IV_SIZE)
		return -EINVAL;

	if (!is_power_of_2(data_unit_size))
		return -EINVAL;

	blk_key->crypto_mode = crypto_mode;
	blk_key->data_unit_size = data_unit_size;
	blk_key->data_unit_size_bits = ilog2(data_unit_size);
	blk_key->size = raw_key_size;
	blk_key->is_hw_wrapped = is_hw_wrapped;
	memcpy(blk_key->raw, raw_key, raw_key_size);

	/*
	 * The keyslot manager uses the SipHash of the key to implement O(1) key
	 * lookups while avoiding leaking information about the keys.  It's
	 * precomputed here so that it only needs to be computed once per key.
	 */
	get_random_once(&hash_key, sizeof(hash_key));
	hash = (u32)siphash(raw_key, raw_key_size, &hash_key);
	blk_crypto_key_set_hash_and_dun_bytes(blk_key, hash, dun_bytes);

	return 0;
}
EXPORT_SYMBOL_GPL(blk_crypto_init_key);

/**
 * blk_crypto_start_using_mode() - Start using blk-crypto on a device
 * @crypto_mode: the crypto mode that will be used
 * @dun_bytes: number of bytes that will be used to specify the DUN
 * @data_unit_size: the data unit size that will be used
 * @is_hw_wrapped_key: whether the key will be hardware-wrapped
 * @q: the request queue for the device
 *
 * Upper layers must call this function to ensure that either the hardware
 * supports the needed crypto settings, or the crypto API fallback has
 * transforms for the needed mode allocated and ready to go.
 *
 * Return: 0 on success; -ENOPKG if the hardware doesn't support the crypto
 *	   settings and blk-crypto-fallback is either disabled or the needed
 *	   algorithm is disabled in the crypto API; or another -errno code.
 */
int blk_crypto_start_using_mode(enum blk_crypto_mode_num crypto_mode,
				unsigned int dun_bytes,
				unsigned int data_unit_size,
				bool is_hw_wrapped_key,
				struct request_queue *q)
{
	if (keyslot_manager_crypto_mode_supported(q->ksm, crypto_mode,
						  dun_bytes, data_unit_size,
						  is_hw_wrapped_key))
		return 0;
	if (is_hw_wrapped_key) {
		pr_warn_once("hardware doesn't support wrapped keys\n");
		return -EOPNOTSUPP;
	}
	return blk_crypto_fallback_start_using_mode(crypto_mode);
}
EXPORT_SYMBOL_GPL(blk_crypto_start_using_mode);

/**
 * blk_crypto_evict_key() - Evict a key from any inline encryption hardware
 *			    it may have been programmed into
 * @q: The request queue who's keyslot manager this key might have been
 *     programmed into
 * @key: The key to evict
 *
 * Upper layers (filesystems) should call this function to ensure that a key
 * is evicted from hardware that it might have been programmed into. This
 * will call keyslot_manager_evict_key on the queue's keyslot manager, if one
 * exists, and supports the crypto algorithm with the specified data unit size.
 * Otherwise, it will evict the key from the blk-crypto-fallback's ksm.
 *
 * Return: 0 on success, -err on error.
 */
int blk_crypto_evict_key(struct request_queue *q,
			 const struct blk_crypto_key *key)
{
	if (q->ksm &&
	    keyslot_manager_crypto_mode_supported(q->ksm, key->crypto_mode,
						  blk_crypto_key_dun_bytes(key),
						  key->data_unit_size,
						  key->is_hw_wrapped))
		return keyslot_manager_evict_key(q->ksm, key);

	return blk_crypto_fallback_evict_key(key);
}
EXPORT_SYMBOL_GPL(blk_crypto_evict_key);
=======
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/keyslot-manager.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "blk-crypto-internal.h"

const struct blk_crypto_mode blk_crypto_modes[] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.cipher_str = "xts(aes)",
		.keysize = 64,
		.ivsize = 16,
	},
	[BLK_ENCRYPTION_MODE_AES_128_CBC_ESSIV] = {
		.cipher_str = "essiv(cbc(aes),sha256)",
		.keysize = 16,
		.ivsize = 16,
	},
	[BLK_ENCRYPTION_MODE_ADIANTUM] = {
		.cipher_str = "adiantum(xchacha12,aes)",
		.keysize = 32,
		.ivsize = 32,
	},
};

/*
 * This number needs to be at least (the number of threads doing IO
 * concurrently) * (maximum recursive depth of a bio), so that we don't
 * deadlock on crypt_ctx allocations. The default is chosen to be the same
 * as the default number of post read contexts in both EXT4 and F2FS.
 */
static int num_prealloc_crypt_ctxs = 128;

module_param(num_prealloc_crypt_ctxs, int, 0444);
MODULE_PARM_DESC(num_prealloc_crypt_ctxs,
		"Number of bio crypto contexts to preallocate");

static struct kmem_cache *bio_crypt_ctx_cache;
static mempool_t *bio_crypt_ctx_pool;

static int __init bio_crypt_ctx_init(void)
{
	size_t i;

	bio_crypt_ctx_cache = KMEM_CACHE(bio_crypt_ctx, 0);
	if (!bio_crypt_ctx_cache)
		goto out_no_mem;

	bio_crypt_ctx_pool = mempool_create_slab_pool(num_prealloc_crypt_ctxs,
						      bio_crypt_ctx_cache);
	if (!bio_crypt_ctx_pool)
		goto out_no_mem;

	/* This is assumed in various places. */
	BUILD_BUG_ON(BLK_ENCRYPTION_MODE_INVALID != 0);

	/* Sanity check that no algorithm exceeds the defined limits. */
	for (i = 0; i < BLK_ENCRYPTION_MODE_MAX; i++) {
		BUG_ON(blk_crypto_modes[i].keysize > BLK_CRYPTO_MAX_KEY_SIZE);
		BUG_ON(blk_crypto_modes[i].ivsize > BLK_CRYPTO_MAX_IV_SIZE);
	}

	return 0;
out_no_mem:
	panic("Failed to allocate mem for bio crypt ctxs\n");
}
subsys_initcall(bio_crypt_ctx_init);

void bio_crypt_set_ctx(struct bio *bio, const struct blk_crypto_key *key,
		       const u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE], gfp_t gfp_mask)
{
	struct bio_crypt_ctx *bc = mempool_alloc(bio_crypt_ctx_pool, gfp_mask);

	bc->bc_key = key;
	memcpy(bc->bc_dun, dun, sizeof(bc->bc_dun));

	bio->bi_crypt_context = bc;
}

void __bio_crypt_free_ctx(struct bio *bio)
{
	mempool_free(bio->bi_crypt_context, bio_crypt_ctx_pool);
	bio->bi_crypt_context = NULL;
}

void __bio_crypt_clone(struct bio *dst, struct bio *src, gfp_t gfp_mask)
{
	dst->bi_crypt_context = mempool_alloc(bio_crypt_ctx_pool, gfp_mask);
	*dst->bi_crypt_context = *src->bi_crypt_context;
}
EXPORT_SYMBOL_GPL(__bio_crypt_clone);

/* Increments @dun by @inc, treating @dun as a multi-limb integer. */
void bio_crypt_dun_increment(u64 dun[BLK_CRYPTO_DUN_ARRAY_SIZE],
			     unsigned int inc)
{
	int i;

	for (i = 0; inc && i < BLK_CRYPTO_DUN_ARRAY_SIZE; i++) {
		dun[i] += inc;
		/*
		 * If the addition in this limb overflowed, then we need to
		 * carry 1 into the next limb. Else the carry is 0.
		 */
		if (dun[i] < inc)
			inc = 1;
		else
			inc = 0;
	}
}

void __bio_crypt_advance(struct bio *bio, unsigned int bytes)
{
	struct bio_crypt_ctx *bc = bio->bi_crypt_context;

	bio_crypt_dun_increment(bc->bc_dun,
				bytes >> bc->bc_key->data_unit_size_bits);
}

/*
 * Returns true if @bc->bc_dun plus @bytes converted to data units is equal to
 * @next_dun, treating the DUNs as multi-limb integers.
 */
bool bio_crypt_dun_is_contiguous(const struct bio_crypt_ctx *bc,
				 unsigned int bytes,
				 const u64 next_dun[BLK_CRYPTO_DUN_ARRAY_SIZE])
{
	int i;
	unsigned int carry = bytes >> bc->bc_key->data_unit_size_bits;

	for (i = 0; i < BLK_CRYPTO_DUN_ARRAY_SIZE; i++) {
		if (bc->bc_dun[i] + carry != next_dun[i])
			return false;
		/*
		 * If the addition in this limb overflowed, then we need to
		 * carry 1 into the next limb. Else the carry is 0.
		 */
		if ((bc->bc_dun[i] + carry) < carry)
			carry = 1;
		else
			carry = 0;
	}

	/* If the DUN wrapped through 0, don't treat it as contiguous. */
	return carry == 0;
}

/*
 * Checks that two bio crypt contexts are compatible - i.e. that
 * they are mergeable except for data_unit_num continuity.
 */
static bool bio_crypt_ctx_compatible(struct bio_crypt_ctx *bc1,
				     struct bio_crypt_ctx *bc2)
{
	if (!bc1)
		return !bc2;

	return bc2 && bc1->bc_key == bc2->bc_key;
}

bool bio_crypt_rq_ctx_compatible(struct request *rq, struct bio *bio)
{
	return bio_crypt_ctx_compatible(rq->crypt_ctx, bio->bi_crypt_context);
}

/*
 * Checks that two bio crypt contexts are compatible, and also
 * that their data_unit_nums are continuous (and can hence be merged)
 * in the order @bc1 followed by @bc2.
 */
bool bio_crypt_ctx_mergeable(struct bio_crypt_ctx *bc1, unsigned int bc1_bytes,
			     struct bio_crypt_ctx *bc2)
{
	if (!bio_crypt_ctx_compatible(bc1, bc2))
		return false;

	return !bc1 || bio_crypt_dun_is_contiguous(bc1, bc1_bytes, bc2->bc_dun);
}

/* Check that all I/O segments are data unit aligned. */
static bool bio_crypt_check_alignment(struct bio *bio)
{
	const unsigned int data_unit_size =
		bio->bi_crypt_context->bc_key->crypto_cfg.data_unit_size;
	struct bvec_iter iter;
	struct bio_vec bv;

	bio_for_each_segment(bv, bio, iter) {
		if (!IS_ALIGNED(bv.bv_len | bv.bv_offset, data_unit_size))
			return false;
	}

	return true;
}

blk_status_t __blk_crypto_init_request(struct request *rq)
{
	return blk_ksm_get_slot_for_key(rq->q->ksm, rq->crypt_ctx->bc_key,
					&rq->crypt_keyslot);
}

/**
 * __blk_crypto_free_request - Uninitialize the crypto fields of a request.
 *
 * @rq: The request whose crypto fields to uninitialize.
 *
 * Completely uninitializes the crypto fields of a request. If a keyslot has
 * been programmed into some inline encryption hardware, that keyslot is
 * released. The rq->crypt_ctx is also freed.
 */
void __blk_crypto_free_request(struct request *rq)
{
	blk_ksm_put_slot(rq->crypt_keyslot);
	mempool_free(rq->crypt_ctx, bio_crypt_ctx_pool);
	blk_crypto_rq_set_defaults(rq);
}

/**
 * __blk_crypto_bio_prep - Prepare bio for inline encryption
 *
 * @bio_ptr: pointer to original bio pointer
 *
 * If the bio crypt context provided for the bio is supported by the underlying
 * device's inline encryption hardware, do nothing.
 *
 * Otherwise, try to perform en/decryption for this bio by falling back to the
 * kernel crypto API. When the crypto API fallback is used for encryption,
 * blk-crypto may choose to split the bio into 2 - the first one that will
 * continue to be processed and the second one that will be resubmitted via
 * generic_make_request. A bounce bio will be allocated to encrypt the contents
 * of the aforementioned "first one", and *bio_ptr will be updated to this
 * bounce bio.
 *
 * Caller must ensure bio has bio_crypt_ctx.
 *
 * Return: true on success; false on error (and bio->bi_status will be set
 *	   appropriately, and bio_endio() will have been called so bio
 *	   submission should abort).
 */
bool __blk_crypto_bio_prep(struct bio **bio_ptr)
{
	struct bio *bio = *bio_ptr;
	const struct blk_crypto_key *bc_key = bio->bi_crypt_context->bc_key;

	/* Error if bio has no data. */
	if (WARN_ON_ONCE(!bio_has_data(bio))) {
		bio->bi_status = BLK_STS_IOERR;
		goto fail;
	}

	if (!bio_crypt_check_alignment(bio)) {
		bio->bi_status = BLK_STS_IOERR;
		goto fail;
	}

	/*
	 * Success if device supports the encryption context, or if we succeeded
	 * in falling back to the crypto API.
	 */
	if (blk_ksm_crypto_cfg_supported(bio->bi_disk->queue->ksm,
					 &bc_key->crypto_cfg))
		return true;

	if (blk_crypto_fallback_bio_prep(bio_ptr))
		return true;
fail:
	bio_endio(*bio_ptr);
	return false;
}

/**
 * __blk_crypto_rq_bio_prep - Prepare a request's crypt_ctx when its first bio
 *			      is inserted
 *
 * @rq: The request to prepare
 * @bio: The first bio being inserted into the request
 * @gfp_mask: gfp mask
 */
void __blk_crypto_rq_bio_prep(struct request *rq, struct bio *bio,
			      gfp_t gfp_mask)
{
	if (!rq->crypt_ctx)
		rq->crypt_ctx = mempool_alloc(bio_crypt_ctx_pool, gfp_mask);
	*rq->crypt_ctx = *bio->bi_crypt_context;
}

/**
 * blk_crypto_init_key() - Prepare a key for use with blk-crypto
 * @blk_key: Pointer to the blk_crypto_key to initialize.
 * @raw_key: Pointer to the raw key. Must be the correct length for the chosen
 *	     @crypto_mode; see blk_crypto_modes[].
 * @crypto_mode: identifier for the encryption algorithm to use
 * @dun_bytes: number of bytes that will be used to specify the DUN when this
 *	       key is used
 * @data_unit_size: the data unit size to use for en/decryption
 *
 * Return: 0 on success, -errno on failure.  The caller is responsible for
 *	   zeroizing both blk_key and raw_key when done with them.
 */
int blk_crypto_init_key(struct blk_crypto_key *blk_key, const u8 *raw_key,
			enum blk_crypto_mode_num crypto_mode,
			unsigned int dun_bytes,
			unsigned int data_unit_size)
{
	const struct blk_crypto_mode *mode;

	memset(blk_key, 0, sizeof(*blk_key));

	if (crypto_mode >= ARRAY_SIZE(blk_crypto_modes))
		return -EINVAL;

	mode = &blk_crypto_modes[crypto_mode];
	if (mode->keysize == 0)
		return -EINVAL;

	if (dun_bytes == 0 || dun_bytes > BLK_CRYPTO_MAX_IV_SIZE)
		return -EINVAL;

	if (!is_power_of_2(data_unit_size))
		return -EINVAL;

	blk_key->crypto_cfg.crypto_mode = crypto_mode;
	blk_key->crypto_cfg.dun_bytes = dun_bytes;
	blk_key->crypto_cfg.data_unit_size = data_unit_size;
	blk_key->data_unit_size_bits = ilog2(data_unit_size);
	blk_key->size = mode->keysize;
	memcpy(blk_key->raw, raw_key, mode->keysize);

	return 0;
}

/*
 * Check if bios with @cfg can be en/decrypted by blk-crypto (i.e. either the
 * request queue it's submitted to supports inline crypto, or the
 * blk-crypto-fallback is enabled and supports the cfg).
 */
bool blk_crypto_config_supported(struct request_queue *q,
				 const struct blk_crypto_config *cfg)
{
	return IS_ENABLED(CONFIG_BLK_INLINE_ENCRYPTION_FALLBACK) ||
	       blk_ksm_crypto_cfg_supported(q->ksm, cfg);
}

/**
 * blk_crypto_start_using_key() - Start using a blk_crypto_key on a device
 * @key: A key to use on the device
 * @q: the request queue for the device
 *
 * Upper layers must call this function to ensure that either the hardware
 * supports the key's crypto settings, or the crypto API fallback has transforms
 * for the needed mode allocated and ready to go. This function may allocate
 * an skcipher, and *should not* be called from the data path, since that might
 * cause a deadlock
 *
 * Return: 0 on success; -ENOPKG if the hardware doesn't support the key and
 *	   blk-crypto-fallback is either disabled or the needed algorithm
 *	   is disabled in the crypto API; or another -errno code.
 */
int blk_crypto_start_using_key(const struct blk_crypto_key *key,
			       struct request_queue *q)
{
	if (blk_ksm_crypto_cfg_supported(q->ksm, &key->crypto_cfg))
		return 0;
	return blk_crypto_fallback_start_using_mode(key->crypto_cfg.crypto_mode);
}

/**
 * blk_crypto_evict_key() - Evict a key from any inline encryption hardware
 *			    it may have been programmed into
 * @q: The request queue who's associated inline encryption hardware this key
 *     might have been programmed into
 * @key: The key to evict
 *
 * Upper layers (filesystems) must call this function to ensure that a key is
 * evicted from any hardware that it might have been programmed into.  The key
 * must not be in use by any in-flight IO when this function is called.
 *
 * Return: 0 on success or if key is not present in the q's ksm, -err on error.
 */
int blk_crypto_evict_key(struct request_queue *q,
			 const struct blk_crypto_key *key)
{
	if (blk_ksm_crypto_cfg_supported(q->ksm, &key->crypto_cfg))
		return blk_ksm_evict_key(q->ksm, key);

	/*
	 * If the request queue's associated inline encryption hardware didn't
	 * have support for the key, then the key might have been programmed
	 * into the fallback keyslot manager, so try to evict from there.
	 */
	return blk_crypto_fallback_evict_key(key);
}
>>>>>>> BRANCH (d1b648 f2fs: should put a page beyond EOF when preparing a write)
