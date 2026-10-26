// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026
 * Author: Nguyen Duy Thanh <nekkochan0x0007@proton.me>
 */

#ifndef PRNG_H
#define PRNG_H

#include <linux/types.h>

#define ROTL32(v, n) (((v) << (n)) | ((v) >> (32 - (n))))
#define ROTL64(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define CHACHA20_QUARTERROUND(a, b, c, d) \
    a += b; d ^= a; d = ROTL32(d, 16); \
    c += d; b ^= c; b = ROTL32(b, 12); \
    a += b; d ^= a; d = ROTL32(d, 8);  \
    c += d; b ^= c; b = ROTL32(b, 7);

struct chacha20_pure_ctx {
    uint32_t state[16];
};

static inline void chacha20_init(struct chacha20_pure_ctx *ctx, const uint32_t key[8], const uint32_t nonce[3], uint32_t counter) {
    ctx->state[0]  = 0x61707865;
    ctx->state[1]  = 0x33322064;
    ctx->state[2]  = 0x79622d32;
    ctx->state[3]  = 0x6b206574;

    for (int i = 0; i < 8; i++) {
        ctx->state[4 + i] = key[i];
    }

    ctx->state[12] = counter;
    ctx->state[13] = nonce[0];
    ctx->state[14] = nonce[1];
    ctx->state[15] = nonce[2];
}

static inline void chacha20_block(struct chacha20_pure_ctx *ctx, uint32_t output[16]) {
    uint32_t x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = ctx->state[i];
    }

    for (int i = 0; i < 10; i++) {
        CHACHA20_QUARTERROUND(x[0], x[4], x[8],  x[12]);
        CHACHA20_QUARTERROUND(x[1], x[5], x[9],  x[13]);
        CHACHA20_QUARTERROUND(x[2], x[6], x[10], x[14]);
        CHACHA20_QUARTERROUND(x[3], x[7], x[11], x[15]);
        CHACHA20_QUARTERROUND(x[0], x[5], x[10], x[15]);
        CHACHA20_QUARTERROUND(x[1], x[6], x[11], x[12]);
        CHACHA20_QUARTERROUND(x[2], x[7], x[8],  x[13]);
        CHACHA20_QUARTERROUND(x[3], x[4], x[9],  x[14]);
    }

    for (int i = 0; i < 16; i++) {
        output[i] = x[i] + ctx->state[i];
    }

    ctx->state[12]++;
}

#endif
