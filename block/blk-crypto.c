// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */
#include <linux/blk-crypto.h>
#include <linux/keyslot-manager.h>
#include <linux/mempool.h>
#include <linux/blk-cgroup.h>
#include <crypto/skcipher.h>
#include <crypto/algapi.h>

struct blk_crypt_mode {
	const char *friendly_name;
	const char *cipher_str;
	size_t keysize;
	size_t ivsize;
	bool needs_essiv;
};

static const struct blk_crypt_mode blk_crypt_modes[] = {
	[BLK_ENCRYPTION_MODE_AES_256_XTS] = {
		.friendly_name = "AES-256-XTS",
		.cipher_str = "xts(aes)",
		.keysize = 64,
		.ivsize = 16,
	},
	/* TODO: the rest of the algs that fscrypt supports */
};

#define BLK_CRYPTO_MAX_KEY_SIZE 64
/* TODO: Do we want to make this user configurable somehow? */
#define BLK_CRYPTO_NUM_KEYSLOTS 100

static struct blk_crypto_keyslot {
	struct crypto_skcipher *tfm;
	enum blk_crypt_mode_num crypt_mode;
	u8 key[BLK_CRYPTO_MAX_KEY_SIZE];
} *blk_crypto_keyslots;

struct work_mem {
	struct work_struct crypto_work;
	struct bio *bio;
};

static struct keyslot_manager *blk_crypto_ksm;
static struct workqueue_struct *blk_crypto_wq;
static mempool_t *blk_crypto_page_pool;
static struct kmem_cache *blk_crypto_work_mem_cache;

static unsigned int num_prealloc_bounce_pg = 32;

bool bio_crypt_swhandled(struct bio *bio)
{
	return bio_crypt_has_keyslot(bio) &&
	       bio->bi_crypt_context->processing_ksm == blk_crypto_ksm;
}

/* TODO: handle modes that need essiv */
static int blk_crypto_keyslot_program(void *priv, const u8 *key,
				      enum blk_crypt_mode_num crypt_mode,
				      unsigned int data_unit_size,
				      unsigned int slot)
{
	struct blk_crypto_keyslot *slotp = &blk_crypto_keyslots[slot];
	struct crypto_skcipher *tfm = slotp->tfm;
	const struct blk_crypt_mode *mode = &blk_crypt_modes[crypt_mode];
	size_t keysize = mode->keysize;
	int err;

	if (crypt_mode != slotp->crypt_mode || !tfm) {
		crypto_free_skcipher(slotp->tfm);
		slotp->tfm = NULL;
		memset(slotp->key, 0, BLK_CRYPTO_MAX_KEY_SIZE);
		tfm = crypto_alloc_skcipher(
			mode->cipher_str, 0, 0);
		if (IS_ERR(tfm))
			return PTR_ERR(tfm);

		crypto_skcipher_set_flags(tfm,
					  CRYPTO_TFM_REQ_WEAK_KEY);
		slotp->crypt_mode = crypt_mode;
		slotp->tfm = tfm;
	}

	err = crypto_skcipher_setkey(tfm, key, keysize);

	if (err) {
		crypto_free_skcipher(tfm);
		slotp->tfm = NULL;
		return err;
	}

	memcpy(slotp->key, key, keysize);

	return 0;
}

static int blk_crypto_keyslot_evict(void *priv, const u8 *key,
				    enum blk_crypt_mode_num crypt_mode,
				    unsigned int data_unit_size,
				    unsigned int slot)
{
	crypto_free_skcipher(blk_crypto_keyslots[slot].tfm);
	blk_crypto_keyslots[slot].tfm = NULL;
	memset(blk_crypto_keyslots[slot].key, 0, BLK_CRYPTO_MAX_KEY_SIZE);

	return 0;
}

static int blk_crypto_keyslot_find(void *priv,
				   const u8 *key,
				   enum blk_crypt_mode_num crypt_mode,
				   unsigned int data_unit_size_bytes)
{
	int slot;
	const size_t keysize = blk_crypt_modes[crypt_mode].keysize;

	/* TODO: hashmap? */
	for (slot = 0; slot < BLK_CRYPTO_NUM_KEYSLOTS; slot++) {
		if (blk_crypto_keyslots[slot].crypt_mode == crypt_mode &&
		    !crypto_memneq(blk_crypto_keyslots[slot].key, key,
				   keysize)) {
			return slot;
		}
	}

	return -ENOKEY;
}

static bool blk_crypt_mode_supported(void *priv,
				     enum blk_crypt_mode_num crypt_mode,
				     unsigned int data_unit_size)
{
	// Of course, blk-crypto supports all blk_crypt_modes.
	return true;
}

static const struct keyslot_mgmt_ll_ops blk_crypto_ksm_ll_ops = {
	.keyslot_program	= blk_crypto_keyslot_program,
	.keyslot_evict		= blk_crypto_keyslot_evict,
	.keyslot_find		= blk_crypto_keyslot_find,
	.crypt_mode_supported	= blk_crypt_mode_supported,
};

static void blk_crypto_put_keyslot(struct bio *bio)
{
	struct bio_crypt_ctx *crypt_ctx = bio->bi_crypt_context;

	keyslot_manager_put_slot(crypt_ctx->processing_ksm, crypt_ctx->keyslot);
	bio_crypt_unset_keyslot(bio);
}

static int blk_crypto_get_keyslot(struct bio *bio,
				      struct keyslot_manager *ksm)
{
	int slot;
	enum blk_crypt_mode_num crypt_mode = bio_crypt_mode(bio);

	if (!ksm)
		return -ENOMEM;

	slot = keyslot_manager_get_slot_for_key(ksm,
						bio_crypt_raw_key(bio),
						crypt_mode, PAGE_SIZE);
	if (slot < 0)
		return slot;

	bio_crypt_set_keyslot(bio, slot, ksm);
	return 0;
}

static void blk_crypto_encrypt_endio(struct bio *enc_bio)
{
	struct bio *src_bio = enc_bio->bi_private;
	struct bio_vec *enc_bvec, *enc_bvec_end;

	enc_bvec = enc_bio->bi_io_vec;
	enc_bvec_end = enc_bvec + enc_bio->bi_vcnt;
	for (; enc_bvec != enc_bvec_end; enc_bvec++)
		mempool_free(enc_bvec->bv_page, blk_crypto_page_pool);

	src_bio->bi_status = enc_bio->bi_status;

	bio_put(enc_bio);
	bio_endio(src_bio);
}

static struct bio *blk_crypto_clone_bio(struct bio *bio_src)
{
	struct bvec_iter iter;
	struct bio_vec bv;
	struct bio *bio;

	bio = bio_alloc_bioset(GFP_NOIO, bio_segments(bio_src), NULL);
	if (!bio)
		return NULL;
	bio->bi_disk		= bio_src->bi_disk;
	bio->bi_opf		= bio_src->bi_opf;
	bio->bi_ioprio		= bio_src->bi_ioprio;
	bio->bi_write_hint	= bio_src->bi_write_hint;
	bio->bi_iter.bi_sector	= bio_src->bi_iter.bi_sector;
	bio->bi_iter.bi_size	= bio_src->bi_iter.bi_size;

	bio_for_each_segment(bv, bio_src, iter)
		bio->bi_io_vec[bio->bi_vcnt++] = bv;

	if (bio_integrity(bio_src)) {
		int ret;

		ret = bio_integrity_clone(bio, bio_src, GFP_NOIO);
		if (ret < 0) {
			bio_put(bio);
			return NULL;
		}
	}

	bio_clone_blkcg_association(bio, bio_src);

	return bio;
}

static int blk_crypto_encrypt_bio(struct bio **bio_ptr)
{
	struct bio *src_bio = *bio_ptr;
	int slot;
	struct skcipher_request *ciph_req = NULL;
	DECLARE_CRYPTO_WAIT(wait);
	struct bio_vec bv;
	struct bvec_iter iter;
	int err = 0;
	u64 curr_dun;
	union {
		__le64 dun;
		u8 bytes[16];
	} iv;
	struct scatterlist src, dst;
	struct bio *enc_bio;
	struct bio_vec *enc_bvec;
	int i, j;
	unsigned int num_sectors;

	if (!blk_crypto_keyslots)
		return -ENOMEM;

	/* Split the bio if it's too big for single page bvec */
	i = 0;
	num_sectors = 0;
	bio_for_each_segment(bv, src_bio, iter) {
		num_sectors += bv.bv_len >> 9;
		if (++i == BIO_MAX_PAGES)
			break;
	}
	if (num_sectors < bio_sectors(src_bio)) {
		struct bio *split_bio;

		split_bio = bio_split(src_bio, num_sectors, GFP_NOIO, NULL);
		if (!split_bio) {
			src_bio->bi_status = BLK_STS_RESOURCE;
			return -ENOMEM;
		}
		bio_chain(split_bio, src_bio);
		generic_make_request(src_bio);
		*bio_ptr = split_bio;
	}

	src_bio = *bio_ptr;

	enc_bio = blk_crypto_clone_bio(src_bio);
	if (!enc_bio) {
		src_bio->bi_status = BLK_STS_RESOURCE;
		return -ENOMEM;
	}

	err = blk_crypto_get_keyslot(src_bio, blk_crypto_ksm);
	if (err) {
		src_bio->bi_status = BLK_STS_IOERR;
		bio_put(enc_bio);
		return err;
	}
	slot = bio_crypt_get_slot(src_bio);

	ciph_req = skcipher_request_alloc(blk_crypto_keyslots[slot].tfm,
					  GFP_NOIO);
	if (!ciph_req) {
		src_bio->bi_status = BLK_STS_RESOURCE;
		err = -ENOMEM;
		bio_put(enc_bio);
		goto out_release_keyslot;
	}

	skcipher_request_set_callback(ciph_req,
				      CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &wait);

	curr_dun = bio_crypt_sw_data_unit_num(src_bio);
	sg_init_table(&src, 1);
	sg_init_table(&dst, 1);
	for (i = 0, enc_bvec = enc_bio->bi_io_vec; i < enc_bio->bi_vcnt;
	     enc_bvec++, i++) {
		struct page *page = enc_bvec->bv_page;
		struct page *ciphertext_page =
			mempool_alloc(blk_crypto_page_pool, GFP_NOFS);

		enc_bvec->bv_page = ciphertext_page;

		if (!ciphertext_page)
			goto no_mem_for_ciph_page;

		memset(&iv, 0, sizeof(iv));
		iv.dun = cpu_to_le64(curr_dun);

		sg_set_page(&src, page, enc_bvec->bv_len, enc_bvec->bv_offset);
		sg_set_page(&dst, ciphertext_page, enc_bvec->bv_len,
			    enc_bvec->bv_offset);

		skcipher_request_set_crypt(ciph_req, &src, &dst,
					   enc_bvec->bv_len, iv.bytes);
		err = crypto_wait_req(crypto_skcipher_encrypt(ciph_req), &wait);
		if (err)
			goto no_mem_for_ciph_page;

		curr_dun++;
		continue;
no_mem_for_ciph_page:
		err = -ENOMEM;
		for (j = i - 1; j >= 0; j--) {
			mempool_free(enc_bio->bi_io_vec->bv_page,
				     blk_crypto_page_pool);
		}
		bio_put(enc_bio);
		goto out_release_cipher;
	}

	enc_bio->bi_private = src_bio;
	enc_bio->bi_end_io = blk_crypto_encrypt_endio;

	*bio_ptr = enc_bio;
out_release_cipher:
	skcipher_request_free(ciph_req);
out_release_keyslot:
	blk_crypto_put_keyslot(src_bio);
	return err;
}

/*
 * TODO: assumption right now is:
 * each segment in bio has length == the data_unit_size
 */
static void blk_crypto_decrypt_bio(struct work_struct *w)
{
	struct work_mem *work_mem =
		container_of(w, struct work_mem, crypto_work);
	struct bio *bio = work_mem->bio;
	int slot = bio_crypt_get_slot(bio);
	struct skcipher_request *ciph_req;
	DECLARE_CRYPTO_WAIT(wait);
	struct bio_vec bv;
	struct bvec_iter iter;
	u64 curr_dun;
	union {
		__le64 dun;
		u8 bytes[16];
	} iv;
	struct scatterlist sg;

	curr_dun = bio_crypt_sw_data_unit_num(bio);

	kmem_cache_free(blk_crypto_work_mem_cache, work_mem);
	ciph_req = skcipher_request_alloc(blk_crypto_keyslots[slot].tfm,
					  GFP_NOFS);
	if (!ciph_req) {
		bio->bi_status = BLK_STS_RESOURCE;
		goto out;
	}

	skcipher_request_set_callback(ciph_req,
				      CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &wait);

	sg_init_table(&sg, 1);
	__bio_for_each_segment(bv, bio, iter,
			       bio->bi_crypt_context->crypt_iter) {
		struct page *page = bv.bv_page;
		int err;

		memset(&iv, 0, sizeof(iv));
		iv.dun = cpu_to_le64(curr_dun);

		sg_set_page(&sg, page, bv.bv_len, bv.bv_offset);
		skcipher_request_set_crypt(ciph_req, &sg, &sg,
					   bv.bv_len, iv.bytes);
		err = crypto_wait_req(crypto_skcipher_decrypt(ciph_req), &wait);
		if (err) {
			bio->bi_status = BLK_STS_IOERR;
			goto out;
		}
		curr_dun++;
	}

out:
	skcipher_request_free(ciph_req);
	blk_crypto_put_keyslot(bio);
	bio_endio(bio);
}

static void blk_crypto_queue_decrypt_bio(struct bio *bio)
{
	struct work_mem *work_mem =
		kmem_cache_zalloc(blk_crypto_work_mem_cache, GFP_ATOMIC);

	if (!work_mem) {
		bio->bi_status = BLK_STS_RESOURCE;
		return bio_endio(bio);
	}

	INIT_WORK(&work_mem->crypto_work, blk_crypto_decrypt_bio);
	work_mem->bio = bio;
	queue_work(blk_crypto_wq, &work_mem->crypto_work);
}

/*
 * Ensures that:
 * 1) The bio’s encryption context is programmed into a keyslot in the
 * keyslot manager (KSM) of the request queue that the bio is being submitted
 * to (or the software fallback KSM if the request queue doesn’t have a KSM),
 * and that the processing_ksm in the bi_crypt_context of this bio is set to
 * this KSM.
 *
 * 2) That the bio has a reference to this keyslot in this KSM.
 */
int blk_crypto_submit_bio(struct bio **bio_ptr)
{
	struct bio *bio = *bio_ptr;
	struct request_queue *q;
	int err;
	enum blk_crypt_mode_num crypt_mode;
	struct bio_crypt_ctx *crypt_ctx;

	if (!bio_has_data(bio))
		return 0;

	if (!bio_is_encrypted(bio) || bio_crypt_swhandled(bio))
		return 0;

	crypt_ctx = bio->bi_crypt_context;
	q = bio->bi_disk->queue;
	crypt_mode = bio_crypt_mode(bio);

	if (bio_crypt_has_keyslot(bio)) {
		/* Key already programmed into device? */
		if (q->ksm == crypt_ctx->processing_ksm)
			return 0;

		/* Nope, release the existing keyslot. */
		blk_crypto_put_keyslot(bio);
	}

	/* Get device keyslot if supported */
	if (q->ksm) {
		/* If lower layer device just wants the key, do nothing */
		if (keyslot_manager_is_passthrough(q->ksm))
			return 0;

		err = blk_crypto_get_keyslot(bio, q->ksm);
		if (!err)
			return 0;
	}

	/* Fallback to software crypto */
	if (bio_data_dir(bio) == WRITE) {
		/* Encrypt the data now */
		err = blk_crypto_encrypt_bio(bio_ptr);
		if (err)
			goto out_encrypt_err;
	} else {
		err = blk_crypto_get_keyslot(bio, blk_crypto_ksm);
		if (err)
			goto out_err;
	}
	return 0;
out_err:
	bio->bi_status = BLK_STS_IOERR;
out_encrypt_err:
	bio_endio(bio);
	return err;
}

/*
 * If the bio is not en/decrypted in software, this function releases the
 * reference to the keyslot that blk_crypto_submit_bio got.
 * If blk_crypto_submit_bio decided to fallback to software crypto for this
 * bio, then if the bio is doing a write, we free the allocated bounce pages,
 * and if the bio is doing a read, we queue the bio for decryption into a
 * workqueue and return -EAGAIN. After the bio has been decrypted, we release
 * the keyslot before we call bio_endio(bio).
 */
bool blk_crypto_endio(struct bio *bio)
{
	if (!bio_crypt_has_keyslot(bio))
		return true;

	if (!bio_crypt_swhandled(bio)) {
		blk_crypto_put_keyslot(bio);
		return true;
	}

	/* bio_data_dir(bio) == READ. So decrypt bio */
	blk_crypto_queue_decrypt_bio(bio);
	return false;
}

int __init blk_crypto_init(void)
{
	blk_crypto_ksm = keyslot_manager_create(BLK_CRYPTO_NUM_KEYSLOTS,
						&blk_crypto_ksm_ll_ops,
						NULL);
	if (!blk_crypto_ksm)
		goto out_ksm;

	blk_crypto_wq = alloc_workqueue("blk_crypto_wq",
					WQ_UNBOUND | WQ_HIGHPRI,
					num_online_cpus());
	if (!blk_crypto_wq)
		goto out_wq;

	blk_crypto_keyslots = kzalloc(sizeof(*blk_crypto_keyslots) *
				      BLK_CRYPTO_NUM_KEYSLOTS,
				      GFP_KERNEL);
	if (!blk_crypto_keyslots)
		goto out_blk_crypto_keyslots;

	blk_crypto_page_pool =
		mempool_create_page_pool(num_prealloc_bounce_pg, 0);
	if (!blk_crypto_page_pool)
		goto out_bounce_pool;

	blk_crypto_work_mem_cache = KMEM_CACHE(work_mem, SLAB_RECLAIM_ACCOUNT);
	if (!blk_crypto_work_mem_cache)
		goto out_work_mem_cache;

	return 0;

out_work_mem_cache:
	mempool_destroy(blk_crypto_page_pool);
	blk_crypto_page_pool = NULL;
out_bounce_pool:
	kzfree(blk_crypto_keyslots);
	blk_crypto_keyslots = NULL;
out_blk_crypto_keyslots:
	destroy_workqueue(blk_crypto_wq);
	blk_crypto_wq = NULL;
out_wq:
	keyslot_manager_destroy(blk_crypto_ksm);
	blk_crypto_ksm = NULL;
out_ksm:
	pr_warn("No memory for blk-crypto software fallback.");
	return -ENOMEM;
}
