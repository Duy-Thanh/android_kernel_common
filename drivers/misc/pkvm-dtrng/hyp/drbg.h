// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */

#ifndef __DTRNG_DRBG_H__
#define __DTRNG_DRBG_H__

#include <crypto/sha256_base.h>

#define HMAC_PAD_KEY_SIZE 64

struct hmac_sha256 {
	u8 opad_key[SHA256_BLOCK_SIZE];
	struct sha256_state digest_state;
};

void hmac_sha256_init(struct hmac_sha256 *hmac, const u8 *key, size_t key_size);

static inline void hmac_sha256_update(struct hmac_sha256 *hmac, const u8 *data,
				      size_t data_size)
{
	sha256_update(&hmac->digest_state, data, data_size);
}

void hmac_sha256_finish(struct hmac_sha256 *hmac, u8 *out);

static inline void hmac_sha256(u8 *key, size_t key_size, u8 *data,
			       size_t data_size, u8 *out)
{
	struct hmac_sha256 hmac;

	hmac_sha256_init(&hmac, key, key_size);
	hmac_sha256_update(&hmac, data, data_size);
	hmac_sha256_finish(&hmac, out);
}

#define DRBG256_RESEED_INTERVAL (1ULL << 48)

struct drbg256_generator {
	u8 v[SHA256_DIGEST_SIZE];
	u8 key[SHA256_DIGEST_SIZE];
	u64 req_counter;
};

void drbg256_init(struct drbg256_generator *gen);

void drbg256_update_scatter(struct drbg256_generator *gen, const u8 **data,
			    const size_t *data_size);

static inline void drbg256_update(struct drbg256_generator *gen, const u8 *data,
				  size_t data_size)
{
	const u8 *update_data[] = { data, NULL };
	size_t update_data_size[] = { data_size };
	drbg256_update_scatter(gen, update_data, update_data_size);
}

static inline void drbg256_seed(struct drbg256_generator *gen,
				const u8 *entropy_input, size_t entropy_size,
				const u8 *nonce, size_t nonce_size,
				const u8 *per, size_t per_size)
{
	const u8 *data[] = { entropy_input, nonce, per, NULL };
	size_t data_size[] = { entropy_size, nonce_size, per_size };
	drbg256_update_scatter(gen, data, data_size);
	gen->req_counter = 0;
}

static inline void drbg256_reseed(struct drbg256_generator *gen,
				  const u8 *entropy_input, size_t entropy_size,
				  const u8 *additional_input,
				  size_t additional_size)
{
	const u8 *data[] = { entropy_input, additional_input, NULL };
	size_t data_size[] = { entropy_size, additional_size };
	drbg256_update_scatter(gen, data, data_size);
	gen->req_counter = 0;
}

int drbg256_generate(struct drbg256_generator *gen, const u8 *data,
		     size_t data_size, u8 *out, size_t out_size);

#endif
