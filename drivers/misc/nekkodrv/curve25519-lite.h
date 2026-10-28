#ifndef CURVE25519_LITE_H
#define CURVE25519_LITE_H

#include <linux/types.h>

typedef uint64_t fe[5];

static inline void fe_zero(fe h) {
    h[0] = h[1] = h[2] = h[3] = h[4] = 0;
}

static inline void fe_copy(fe h, const fe f) {
    h[0] = f[0];
    h[1] = f[1];
    h[2] = f[2];
    h[3] = f[3];
    h[4] = f[4];
}

static inline void fe_add(fe h, const fe f, const fe g) {
    h[0] = f[0] + g[0];
    h[1] = f[1] + g[1];
    h[2] = f[2] + g[2];
    h[3] = f[3] + g[3];
    h[4] = f[4] + g[4];
}

static inline void fe_sub(fe h, const fe f, const fe g) {
    h[0] = f[0] + 0x7ffffed + (1ULL << 26) - g[0];
    h[1] = f[1] + 0x7ffffed + (1ULL << 26) - g[1];
    h[2] = f[2] + 0x7ffffed + (1ULL << 26) - g[2];
    h[3] = f[3] + 0x7ffffed + (1ULL << 26) - g[3];
    h[4] = f[4] + 0x7ffffed + (1ULL << 26) - g[4];
}

static void fe_mul(fe h, const fe f, const fe g) {
    uint64_t f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3], f4 = f[4];
    uint64_t g0 = g[0], g1 = g[1], g2 = g[2], g3 = g[3], g4 = g[4];
    uint64_t g1_19 = g1 * 19;
    uint64_t g2_19 = g2 * 19;
    uint64_t g3_19 = g3 * 19;
    uint64_t g4_19 = g4 * 19;

    uint64_t h0 = f0*g0 + f1*g4_19 + f2*g3_19 + f3*g2_19 + f4*g1_19;
    uint64_t h1 = f0*g1 + f1*g0    + f2*g4_19 + f3*g3_19 + f4*g2_19;
    uint64_t h2 = f0*g2 + f1*g1    + f2*g0    + f3*g4_19 + f4*g3_19;
    uint64_t h3 = f0*g3 + f1*g2    + f2*g1    + f3*g0    + f4*g4_19;
    uint64_t h4 = f0*g4 + f1*g3    + f2*g2    + f3*g1    + f4*g0;

    uint64_t c;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
    c = h1 >> 25; h1 &= 0x1ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 25; h3 &= 0x1ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 19;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    h[0] = h0;
    h[1] = h1;
    h[2] = h2;
    h[3] = h3;
    h[4] = h4;
}

static void fe_sq(fe h, const fe f) {
    fe_mul(h, f, f);
}

static void fe_invert(fe out, const fe z) {
    fe t0, t1, t2, t3;
    int i;
    fe_sq(t0, z);
    fe_sq(t1, t0); fe_sq(t1, t1); fe_mul(t1, z, t1);
    fe_mul(t0, t0, t1);
    fe_sq(t2, t0); fe_mul(t2, t1, t2);
    fe_sq(t1, t2); for (i = 1; i < 5; ++i) fe_sq(t1, t1); fe_mul(t1, t1, t2);
    fe_sq(t3, t1); for (i = 1; i < 10; ++i) fe_sq(t3, t3); fe_mul(t3, t3, t1);
    fe_sq(t1, t3); for (i = 1; i < 20; ++i) fe_sq(t1, t1); fe_mul(t1, t1, t3);
    fe_sq(t1, t1); for (i = 1; i < 10; ++i) fe_sq(t1, t1); fe_mul(t1, t1, t2);
    fe_sq(t2, t1); for (i = 1; i < 50; ++i) fe_sq(t2, t2); fe_mul(t2, t2, t1);
    fe_sq(t3, t2); for (i = 1; i < 100; ++i) fe_sq(t3, t3); fe_mul(t3, t3, t2);
    fe_sq(t2, t3); for (i = 1; i < 50; ++i) fe_sq(t2, t2); fe_mul(t2, t2, t1);
    fe_sq(t2, t2); for (i = 1; i < 4; ++i) fe_sq(t2, t2); fe_mul(out, t2, t0);
}

// Simple Montgomery Ladder Point Multiplication for X25519
static void x25519_scalarmult(uint8_t *out, const uint8_t *scalar, const uint8_t *point) {
    uint8_t e[32];
    int i;
    for (i = 0; i < 32; ++i) e[i] = scalar[i];
    e[0] &= 248; e[31] &= 127; e[31] |= 64;

    fe x1, x2, z2, x3, z3, tmp0, tmp1;
    fe_zero(x1); fe_zero(x2); fe_zero(z2); fe_zero(x3); fe_zero(z3);

    if (point) {
        // Decode base point
        uint64_t p0 = 0, p1 = 0, p2 = 0, p3 = 0, p4 = 0;
        // Simplified unpack (assume valid 256-bit point)
        for(i=0; i<32; i++) {
            int bit = i * 8;
            uint64_t val = point[i];
            if (bit < 26) p0 |= val << bit;
            else if (bit < 51) p1 |= val << (bit - 26);
            else if (bit < 77) p2 |= val << (bit - 51);
            else if (bit < 102) p3 |= val << (bit - 77);
            else p4 |= val << (bit - 102);
        }
        x1[0] = p0 & 0x3ffffff; x1[1] = p1 & 0x1ffffff; x1[2] = p2 & 0x3ffffff;
        x1[3] = p3 & 0x1ffffff; x1[4] = p4 & 0x3ffffff;
    } else {
        x1[0] = 9; // Base generator point G = 9
    }

    fe_copy(x2, x1); x2[0] = 1; // 1
    fe_zero(z2);
    fe_copy(x3, x1);
    fe_copy(z3, x2); z3[0] = 1;

    int pos;
    for (pos = 254; pos >= 0; --pos) {
        uint8_t b = (e[pos >> 3] >> (pos & 7)) & 1;
        // Swap conditional
        if (b) {
            fe_sub(tmp0, x2, x3); fe_copy(x2, x3); fe_add(x3, x3, tmp0);
            fe_sub(tmp0, z2, z3); fe_copy(z2, z3); fe_add(z3, z3, tmp0);
        }
        fe_add(tmp0, x3, z3); fe_sub(tmp1, x3, z3);
        fe_add(x3, x2, z2); fe_sub(z3, x2, z2);
        fe_mul(tmp1, tmp1, x3); fe_mul(tmp0, tmp0, z3);
        fe_add(x2, tmp1, tmp0); fe_sub(z2, tmp1, tmp0);
        fe_sq(z2, z2); fe_sq(x2, x2);
        fe_mul(z3, x2, x1); fe_copy(x2, z2);
        // Swap conditional back
        if (b) {
            fe_sub(tmp0, x2, x3); fe_copy(x2, x3); fe_add(x3, x3, tmp0);
            fe_sub(tmp0, z2, z3); fe_copy(z2, z3); fe_add(z3, z3, tmp0);
        }
    }

    fe_invert(tmp0, z2);
    fe_mul(tmp1, x2, tmp0);

    // Pack 256-bit output
    uint64_t t[5];
    fe_copy(t, tmp1);
    for(i = 0; i < 32; i++) {
        out[i] = (uint8_t)((t[i / 7] >> ((i % 7) * 8)) & 0xff);
    }
}

#endif
