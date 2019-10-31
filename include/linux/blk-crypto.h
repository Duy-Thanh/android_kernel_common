/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_BLK_CRYPTO_H
#define __LINUX_BLK_CRYPTO_H

#include <linux/types.h>
#include <linux/bio.h>

#ifdef CONFIG_BLK_INLINE_ENCRYPTION

int blk_crypto_init(void);

int blk_crypto_submit_bio(struct bio **bio_ptr);

bool blk_crypto_endio(struct bio *bio);

int blk_crypto_start_using_mode(enum blk_crypto_mode_num mode_num,
				unsigned int data_unit_size,
				struct request_queue *q);

struct blk_crypto_key *
blk_crypto_alloc_key(const u8 *raw_key, enum blk_crypto_mode_num crypto_mode,
		     unsigned int data_unit_size, gfp_t gfp_flags);

int blk_crypto_evict_key(struct request_queue *q,
			 const struct blk_crypto_key *key);

void blk_crypto_free_key(struct blk_crypto_key *key);

#else /* CONFIG_BLK_INLINE_ENCRYPTION */

static inline int blk_crypto_init(void)
{
	return 0;
}

static inline int blk_crypto_submit_bio(struct bio **bio_ptr)
{
	return 0;
}

static inline bool blk_crypto_endio(struct bio *bio)
{
	return true;
}

static inline int
blk_crypto_start_using_mode(enum blk_crypto_mode_num mode_num,
			    unsigned int data_unit_size,
			    struct request_queue *q)
{
	return -EOPNOTSUPP;
}

static inline struct blk_crypto_key *
blk_crypto_alloc_key(const u8 *raw_key, enum blk_crypto_mode_num crypto_mode,
		     unsigned int data_unit_size, gfp_t gfp_flags)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int blk_crypto_evict_key(struct request_queue *q,
				       const struct blk_crypto_key *key)
{
	return 0;
}

static inline void blk_crypto_free_key(struct blk_crypto_key *key)
{
}

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

#endif /* __LINUX_BLK_CRYPTO_H */
