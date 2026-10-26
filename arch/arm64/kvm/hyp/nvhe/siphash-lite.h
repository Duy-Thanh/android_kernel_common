// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026
 * Author: Nguyen Duy Thanh <nekkochan0x0007@proton.me>
 */

#ifndef SIPHASH_LITE_H
#define SIPHASH_LITE_H

#define ROTL64(x, b)    (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))

#define SIPROUND        \
    do {                \
        v0 += v1; v1 = ROTL64(v1, 13); v1 ^= v0; v0 = ROTL64(v0, 32);   \
        v2 += v3; v3 = ROTL64(v3, 16); v3 ^= v2;                        \
        v0 += v3; v3 = ROTL64(v3, 21); v3 ^= v0;                        \
        v2 += v1; v1 = ROTL64(v1, 17); v1 ^= v2; v2 = ROTL64(v2, 32);   \
    } while (0)

// Calculate HMAC/Hash for 1 block with 64-bit of nonce with 128-bit of shared key
static inline uint64_t siphash24_single_u64(uint64_t nonce, uint64_t k0, uint64_t k1)
{
    uint64_t v0 = 0x736f6d6570736575ULL ^ k0;
    uint64_t v1 = 0x646f72616d706c65ULL ^ k1;
    uint64_t v2 = 0x6c7967656e657261ULL ^ k0;
    uint64_t v3 = 0x7465646279746573ULL ^ k1;

    v3 ^= nonce;
    SIPROUND; SIPROUND;
    v0 ^= nonce;

    v2 ^= 0xff;
    SIPROUND; SIPROUND; SIPROUND; SIPROUND;

    return (v0 ^ v1 ^ v2 ^ v3);
}

// Compare time constants
static inline bool constant_time_compare_u64(uint64_t a, uint64_t b)
{
    uint64_t diff = a ^ b;

    // return true if diff = 0 without branching
    return ((diff | (0 - diff)) >> 63) == 0;
}

#endif
