/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The board's entropy source (SPEC-017 §11, A124): 1 MiB read and tested on the board itself
 * (the bytes' frequencies by chi-square, the balance of ones and zeros, the bit transitions,
 * the longest repetition of a byte), each block through the source's health tests, and the
 * health failures counted. On a physical board (EMB_ENTROPY_DUMP) the first 64 KiB are printed
 * in hex for the host's own checks (tools/entropy/check.py), with the SHA-256 they must have.
 * These are sanity checks of the driver and the source, not a certification of the source.
 * Built where the board has an entropy device, and on the native board's host source. */
#include <emb_test.h>
#include <string.h>

#define TOTAL    (1024u * 1024u)
#define CHUNK    1024u
#define SAMPLE   (64u * 1024u)
#define EXPECT   (TOTAL / 256u) /* each byte value's count */
#define BITS     (8u * TOTAL)
#define SPREAD   8000u           /* some 5.5 standard deviations of the bit tests */
#define LONGEST  5u              /* a run of 6 equal bytes in 1 MiB: about one in a million */
#define FAILURES (TOTAL / 1024u) /* health failures allowed: one per 1024 bytes */

#ifndef EMB_ENTROPY_DUMP
#define EMB_ENTROPY_DUMP 0
#endif

/* ---- SHA-256 (FIPS 180-4), for the fingerprints ------------------------------------- */

typedef struct sha256 {
    uint32_t h[8];
    uint8_t block[64];
    uint32_t used;
    uint64_t bytes;
} sha256_t;

static const uint32_t sha_k[64] = {
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

static uint32_t rotr(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32u - n));
}

static void sha256_round(sha256_t *s)
{
    uint32_t w[64];
    uint32_t v[8];
    unsigned i;
    for (i = 0u; i < 16u; i++) {
        w[i] = ((uint32_t)s->block[4u * i] << 24) | ((uint32_t)s->block[(4u * i) + 1u] << 16) |
               ((uint32_t)s->block[(4u * i) + 2u] << 8) | (uint32_t)s->block[(4u * i) + 3u];
    }
    for (i = 16u; i < 64u; i++) {
        uint32_t s0 = rotr(w[i - 15u], 7u) ^ rotr(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
        uint32_t s1 = rotr(w[i - 2u], 17u) ^ rotr(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    for (i = 0u; i < 8u; i++) {
        v[i] = s->h[i];
    }
    for (i = 0u; i < 64u; i++) {
        uint32_t t1 = v[7] + (rotr(v[4], 6u) ^ rotr(v[4], 11u) ^ rotr(v[4], 25u)) +
                      ((v[4] & v[5]) ^ (~v[4] & v[6])) + sha_k[i] + w[i];
        uint32_t t2 = (rotr(v[0], 2u) ^ rotr(v[0], 13u) ^ rotr(v[0], 22u)) +
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

static void sha256_start(sha256_t *s)
{
    static const uint32_t h0[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    (void)memcpy(s->h, h0, sizeof(h0));
    s->used = 0u;
    s->bytes = 0u;
}

static void sha256_add(sha256_t *s, const uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        s->block[s->used++] = p[i];
        if (s->used == 64u) {
            sha256_round(s);
            s->used = 0u;
        }
    }
    s->bytes += n;
}

static void sha256_hex(sha256_t *s, char *hex)
{
    static const char digits[] = "0123456789abcdef";
    uint64_t bits = s->bytes * 8u;
    uint8_t pad = 0x80u;
    uint8_t len[8];
    unsigned i;
    sha256_add(s, &pad, 1u);
    pad = 0u;
    while (s->used != 56u) {
        sha256_add(s, &pad, 1u);
    }
    for (i = 0u; i < 8u; i++) {
        len[i] = (uint8_t)(bits >> (56u - (8u * i)));
    }
    sha256_add(s, len, 8u);
    for (i = 0u; i < 32u; i++) {
        uint8_t b = (uint8_t)(s->h[i / 4u] >> (24u - (8u * (i % 4u))));
        hex[2u * i] = digits[b >> 4];
        hex[(2u * i) + 1u] = digits[b & 0xFu];
    }
    hex[64] = '\0';
}

/* ---- the run ------------------------------------------------------------------------- */

static uint32_t counts[256];
static uint8_t chunk[CHUNK];

static void dump(const uint8_t *p, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    char line[2u * 32u + 1u];
    size_t at;
    for (at = 0u; at < n; at += 32u) { /* bounded: n */
        unsigned i;
        for (i = 0u; i < 32u; i++) {
            line[2u * i] = digits[p[at + i] >> 4];
            line[(2u * i) + 1u] = digits[p[at + i] & 0xFu];
        }
        line[64] = '\0';
        emb_test_logf(EMB_TEST_STR("ENTROPY %s\n"), line);
    }
}

EMB_TEST(entropy_board_a_mebibyte_passes_the_sanity_checks)
{
    const emb_device_t *dev = NULL;
    sha256_t whole;
    sha256_t sample;
    char hex[65];
    uint32_t done = 0u;
    uint32_t failures = 0u;
    uint32_t ones = 0u;
    uint32_t transitions = 0u;
    uint32_t run = 1u;
    uint32_t longest = 1u;
    uint64_t squares = 0u;
    uint8_t prev_byte = 0u;
    unsigned prev_bit = 0u;
    emb_instant_t t0;
    uint32_t ms;
    unsigned v;
    EMB_TEST_REQ("SEC-012", "DRV-001");
    EMB_ASSERT_OK(emb_drv_entropy_board(&dev));
    (void)memset(counts, 0, sizeof(counts));
    sha256_start(&whole);
    sha256_start(&sample);
    t0 = emb_time_now();
    while (done < TOTAL) { /* bounded: 1 MiB, and the failures allowed */
        size_t got = 0u;
        size_t i;
        emb_status_t st = emb_drv_entropy_read(dev, chunk, CHUNK, &got, EMB_TIMEOUT(EMB_MS(5000u)));
        if (st == EMB_EHEALTH) {
            failures++;
            EMB_ASSERT_TRUE(failures <= FAILURES);
            if (failures > FAILURES) {
                return;
            }
        } else {
            EMB_ASSERT_OK(st);
            if (st != EMB_OK) {
                return;
            }
        }
        if ((done + got) > TOTAL) {
            got = TOTAL - done;
        }
        for (i = 0u; i < got; i++) { /* bounded: a chunk */
            uint8_t b = chunk[i];
            unsigned k;
            counts[b]++;
            if ((done + i) != 0u) {
                run = (b == prev_byte) ? (run + 1u) : 1u;
                longest = (run > longest) ? run : longest;
            }
            prev_byte = b;
            for (k = 0u; k < 8u; k++) { /* bounded: a byte's bits */
                unsigned bit = (b >> k) & 1u;
                ones += bit;
                if (((done + i) != 0u) || (k != 0u)) {
                    transitions += (bit != prev_bit) ? 1u : 0u;
                }
                prev_bit = bit;
            }
        }
        sha256_add(&whole, chunk, got);
        if (done < SAMPLE) {
            size_t n = ((SAMPLE - done) < got) ? (SAMPLE - done) : got;
            sha256_add(&sample, chunk, n);
            if (EMB_ENTROPY_DUMP != 0) {
                dump(chunk, n);
            }
        }
        done += (uint32_t)got;
    }
    ms = (uint32_t)((emb_instant_sub(emb_time_now(), t0).ticks * (emb_tick_t)CONFIG_EMB_TICK_NS) /
                    1000000u);
    for (v = 0u; v < 256u; v++) {
        int64_t d = (int64_t)counts[v] - (int64_t)EXPECT;
        squares += (uint64_t)(d * d);
    }
    emb_test_logf(EMB_TEST_STR("  entropy: %lu bytes in %lu ms, %lu health failures\n"),
                  (unsigned long)done, (unsigned long)ms, (unsigned long)failures);
    emb_test_logf(EMB_TEST_STR("  entropy: chi-square x100 %lu (255 degrees), ones %lu of %lu bits,"
                               " transitions %lu, longest byte repetition %lu\n"),
                  (unsigned long)((squares * 100u) / EXPECT), (unsigned long)ones,
                  (unsigned long)BITS, (unsigned long)transitions, (unsigned long)longest);
    sha256_hex(&sample, hex);
    emb_test_logf(EMB_TEST_STR("ENTROPY sample %lu bytes sha256 %s\n"), (unsigned long)SAMPLE, hex);
    sha256_hex(&whole, hex);
    emb_test_logf(EMB_TEST_STR("  entropy: the mebibyte's sha256 %s\n"), hex);
    /* chi-square with 255 degrees: mean 255, deviation 22.6; these bounds are some 6 of them */
    EMB_ASSERT_TRUE(((squares * 100u) / EXPECT >= 12000u) && ((squares * 100u) / EXPECT <= 40000u));
    EMB_ASSERT_TRUE((ones + SPREAD > BITS / 2u) && (ones < (BITS / 2u) + SPREAD));
    EMB_ASSERT_TRUE((transitions + SPREAD > BITS / 2u) && (transitions < (BITS / 2u) + SPREAD));
    EMB_ASSERT_TRUE(longest <= LONGEST);
}
