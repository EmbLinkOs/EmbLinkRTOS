/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* SHA-256 (FIPS 180-4 §6.2), the entropy class's conditioning function (A124): the message
 * schedule and 64 rounds per 512-bit block, the padding and the length at the end, and the
 * state cleared once the digest is out, since it held the raw entropy. */
#include <embk/sha256.h>

static uint32_t sha256_rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32u - n));
}

static void sha256_block(embk_sha256_t *s)
{
    static const uint32_t sha256_k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
        0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
        0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
        0xc67178f2u};
    uint32_t w[64];
    uint32_t v[8];
    uint32_t i;
    for (i = 0u; i < 16u; i++) { /* bounded: the block's 16 words */
        uint32_t at = 4u * i;
        w[i] = ((uint32_t)s->block[at] << 24) | ((uint32_t)s->block[at + 1u] << 16) |
               ((uint32_t)s->block[at + 2u] << 8) | (uint32_t)s->block[at + 3u];
    }
    for (i = 16u; i < 64u; i++) { /* bounded: the schedule's 64 words */
        uint32_t s0 = sha256_rotr(w[i - 15u], 7u) ^ sha256_rotr(w[i - 15u], 18u) ^
                      (w[i - 15u] >> 3);
        uint32_t s1 = sha256_rotr(w[i - 2u], 17u) ^ sha256_rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    for (i = 0u; i < 8u; i++) {
        v[i] = s->h[i];
    }
    for (i = 0u; i < 64u; i++) { /* bounded: 64 rounds */
        uint32_t t1 = v[7] +
                      (sha256_rotr(v[4], 6u) ^ sha256_rotr(v[4], 11u) ^ sha256_rotr(v[4], 25u)) +
                      ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha256_k[i] + w[i];
        uint32_t t2 = (sha256_rotr(v[0], 2u) ^ sha256_rotr(v[0], 13u) ^ sha256_rotr(v[0], 22u)) +
                      ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + t2;
    }
    for (i = 0u; i < 8u; i++) {
        s->h[i] += v[i];
    }
}

void embk_sha256_start(embk_sha256_t *s)
{
    static const uint32_t h0[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint32_t i;
    for (i = 0u; i < 8u; i++) {
        s->h[i] = h0[i];
    }
    s->used = 0u;
    s->bytes = 0u;
}

void embk_sha256_add(embk_sha256_t *s, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) { /* bounded: n */
        s->block[s->used] = p[i];
        s->used++;
        if (s->used == 64u) {
            sha256_block(s);
            s->used = 0u;
        }
    }
    s->bytes += (uint64_t)n;
}

void embk_sha256_end(embk_sha256_t *s, uint8_t *out)
{
    static const uint8_t one = 0x80u;
    static const uint8_t zero = 0u;
    uint64_t bits = s->bytes * (uint64_t)8u;
    uint8_t len[8];
    uint32_t i;
    volatile uint8_t *wipe = (volatile uint8_t *)s->block;
    embk_sha256_add(s, &one, 1u);
    while (s->used != 56u) { /* bounded: under one block */
        embk_sha256_add(s, &zero, 1u);
    }
    for (i = 0u; i < 8u; i++) { /* the length, most significant byte first */
        uint32_t shift = 56u - (8u * i);
        len[i] = (uint8_t)(bits >> shift);
    }
    embk_sha256_add(s, len, 8u);
    for (i = 0u; i < EMBK_SHA256_SIZE; i++) {
        out[i] = (uint8_t)(s->h[i / 4u] >> (24u - (8u * (i % 4u))));
    }
    for (i = 0u; i < 64u; i++) { /* the raw bytes it held go */
        wipe[i] = 0u;
    }
    for (i = 0u; i < 8u; i++) {
        s->h[i] = 0u;
    }
}
