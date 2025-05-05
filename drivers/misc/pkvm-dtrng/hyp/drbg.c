// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 - Google Inc
 */

#include "drbg.h"
#include "dtrng-common.h"
#include <linux/minmax.h>

static inline void hmac_pad_key(const u8 *key, size_t key_size, u8 *ipad,
				u8 *opad, size_t pad_size)
{
	size_t i;
	u8 b;

	for (i = 0; i < pad_size; i++) {
		b = i < key_size ? key[i] : 0;

		ipad[i] = b ^ 0x5c;
		opad[i] = b ^ 0x36;
	}
}

void hmac_sha256_init(struct hmac_sha256 *hmac, const u8 *key, size_t key_size)
{
	u8 padded_key[SHA256_DIGEST_SIZE];
	u8 ipad_key[SHA256_BLOCK_SIZE];

	if (key_size > SHA256_BLOCK_SIZE) {
		sha256(key, key_size, padded_key);
		hmac_pad_key(padded_key, sizeof(padded_key), ipad_key,
			     hmac->opad_key, sizeof(ipad_key));
	} else {
		hmac_pad_key(key, key_size, ipad_key, hmac->opad_key,
			     sizeof(ipad_key));
	}

	sha256_init(&hmac->digest_state);
	sha256_update(&hmac->digest_state, ipad_key, sizeof(ipad_key));
}

void hmac_sha256_finish(struct hmac_sha256 *hmac, u8 *out)
{
	u8 ihash[SHA256_DIGEST_SIZE];
	struct sha256_state ohash;

	sha256_final(&hmac->digest_state, ihash);

	sha256_init(&ohash);
	sha256_update(&ohash, hmac->opad_key, sizeof(hmac->opad_key));
	sha256_update(&ohash, ihash, sizeof(ihash));
	sha256_final(&ohash, out);
}

void drbg256_init(struct drbg256_generator *gen)
{
	memset(gen->v, 0x01, sizeof(gen->v));
	memset(gen->key, 0x00, sizeof(gen->key));
	gen->req_counter = DRBG256_RESEED_INTERVAL;
}

void drbg256_update_scatter(struct drbg256_generator *gen, const u8 **data,
			    const size_t *data_size)
{
	static const u8 step1_delim[] = { 0x00 };
	static const u8 step4_delim[] = { 0x01 };

	struct hmac_sha256 hmac;
	size_t i;

	/* Step 1 */
	hmac_sha256_init(&hmac, gen->key, sizeof(gen->key));
	hmac_sha256_update(&hmac, gen->v, sizeof(gen->v));
	hmac_sha256_update(&hmac, step1_delim, sizeof(step1_delim));

	for (i = 0; data[i] != NULL; i++)
		hmac_sha256_update(&hmac, data[i], data_size[i]);

	hmac_sha256_finish(&hmac, gen->key);

	/* Step 2 */
	hmac_sha256(gen->key, sizeof(gen->key), gen->v, sizeof(gen->v), gen->v);

	/* Step 3 */
	if (data[0] == NULL)
		return;

	/* Step 4 */
	hmac_sha256_init(&hmac, gen->key, sizeof(gen->key));
	hmac_sha256_update(&hmac, gen->v, sizeof(gen->v));
	hmac_sha256_update(&hmac, step4_delim, sizeof(step4_delim));

	for (i = 0; data[i] != NULL; i++)
		hmac_sha256_update(&hmac, data[i], data_size[i]);

	hmac_sha256_finish(&hmac, gen->key);

	/* Step 5 */
	hmac_sha256(gen->key, sizeof(gen->key), gen->v, sizeof(gen->v), gen->v);
}

int drbg256_generate(struct drbg256_generator *gen, const u8 *data,
		     size_t data_size, u8 *out, size_t out_size)
{
	size_t chunk_size;

	if (gen->req_counter >= DRBG256_RESEED_INTERVAL)
		return -EAGAIN;

	/* Step 2 */
	if (data != NULL)
		drbg256_update(gen, data, data_size);

	/* Step 4 */
	while (out_size > 0) {
		hmac_sha256(gen->key, sizeof(gen->key), gen->v, sizeof(gen->v),
			    gen->v);

		chunk_size = min(out_size, sizeof(gen->v));

		memcpy(out, gen->v, chunk_size);
		out_size -= chunk_size;
	}

	/* Step 6 */
	drbg256_update(gen, data, data_size);

	gen->req_counter++;

	return 0;
}
