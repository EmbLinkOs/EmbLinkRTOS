/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The board's entropy source (SPEC-017 §11, A124). 1 MiB read, conditioned, and tested on the
 * board itself: the bytes' frequencies by chi-square, the balance of ones and zeros, the bit
 * transitions, the longest repetition of a byte; the health failures counted. On a physical
 * board (EMB_ENTROPY_DUMP) the first 64 KiB are printed in hex for the host's own checks
 * (tools/entropy/check.py), with the SHA-256 they must have. Then 256 KiB of the source's raw
 * blocks, measured, not judged by those tests: a raw noise source is biased and correlated,
 * which is why the class conditions it. Its min-entropy is estimated by NIST SP 800-90B's
 * most-common-value test and must reach the 4 bits a byte that conditioning 2:1 needs. These
 * are sanity checks of the driver and the source, not a certification of the source. Built
 * where the board has an entropy device, and on the native board's host source. */
#include <embk/sha256.h>

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

/* The digest as hex. */
static void hex_of(const uint8_t *d, char *hex)
{
    static const char digits[] = "0123456789abcdef";
    unsigned i;
    for (i = 0u; i < EMBK_SHA256_SIZE; i++) {
        hex[2u * i] = digits[d[i] >> 4];
        hex[(2u * i) + 1u] = digits[d[i] & 0xFu];
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
    embk_sha256_t whole;
    embk_sha256_t sample;
    uint8_t digest[EMBK_SHA256_SIZE];
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
    embk_sha256_start(&whole);
    embk_sha256_start(&sample);
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
                unsigned bit = ((unsigned)b >> k) & 1u;
                ones += bit;
                if (((done + i) != 0u) || (k != 0u)) {
                    transitions += (bit != prev_bit) ? 1u : 0u;
                }
                prev_bit = bit;
            }
        }
        embk_sha256_add(&whole, chunk, got);
        if (done < SAMPLE) {
            size_t n = ((SAMPLE - done) < got) ? (SAMPLE - done) : got;
            embk_sha256_add(&sample, chunk, n);
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
    embk_sha256_end(&sample, digest);
    hex_of(digest, hex);
    emb_test_logf(EMB_TEST_STR("ENTROPY sample %lu bytes sha256 %s\n"), (unsigned long)SAMPLE, hex);
    embk_sha256_end(&whole, digest);
    hex_of(digest, hex);
    emb_test_logf(EMB_TEST_STR("  entropy: the mebibyte's sha256 %s\n"), hex);
    /* chi-square with 255 degrees: mean 255, deviation 22.6; these bounds are some 6 of them */
    EMB_ASSERT_TRUE(((squares * 100u) / EXPECT >= 12000u) && ((squares * 100u) / EXPECT <= 40000u));
    EMB_ASSERT_TRUE((ones + SPREAD > BITS / 2u) && (ones < (BITS / 2u) + SPREAD));
    EMB_ASSERT_TRUE((transitions + SPREAD > BITS / 2u) && (transitions < (BITS / 2u) + SPREAD));
    EMB_ASSERT_TRUE(longest <= LONGEST);
}

#define RAW_TOTAL (256u * 1024u)

EMB_TEST(entropy_board_the_raw_source_has_the_entropy_conditioning_needs)
{
    const emb_device_t *dev = NULL;
    uint32_t done = 0u;
    uint32_t failures = 0u;
    uint32_t most = 0u;
    uint32_t transitions = 0u;
    uint64_t squares = 0u;
    unsigned prev_bit = 0u;
    unsigned v;
    EMB_TEST_REQ("DRV-001");
    EMB_ASSERT_OK(emb_drv_entropy_board(&dev));
    (void)memset(counts, 0, sizeof(counts));
    while (done < RAW_TOTAL) { /* bounded: 256 KiB, and the failures allowed */
        size_t got = 0u;
        size_t i;
        emb_status_t st =
            emb_drv_entropy_read_raw(dev, chunk, CHUNK, &got, EMB_TIMEOUT(EMB_MS(5000u)));
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
        if ((done + got) > RAW_TOTAL) {
            got = RAW_TOTAL - done;
        }
        for (i = 0u; i < got; i++) { /* bounded: a chunk */
            unsigned k;
            counts[chunk[i]]++;
            for (k = 0u; k < 8u; k++) { /* bounded: a byte's bits */
                unsigned bit = ((unsigned)chunk[i] >> k) & 1u;
                if (((done + i) != 0u) || (k != 0u)) {
                    transitions += (bit != prev_bit) ? 1u : 0u;
                }
                prev_bit = bit;
            }
        }
        done += (uint32_t)got;
    }
    for (v = 0u; v < 256u; v++) {
        int64_t d = (int64_t)counts[v] - (int64_t)(RAW_TOTAL / 256u);
        squares += (uint64_t)(d * d);
        most = (counts[v] > most) ? counts[v] : most;
    }
    emb_test_logf(
        EMB_TEST_STR("ENTROPY raw %lu bytes, most common value %lu times, chi-square x100 %lu,"
                     " transitions %lu of %lu, %lu health failures\n"),
        (unsigned long)done, (unsigned long)most,
        (unsigned long)((squares * 100u) / (RAW_TOTAL / 256u)), (unsigned long)transitions,
        (unsigned long)((8u * RAW_TOTAL) - 1u), (unsigned long)failures);
    /* SP 800-90B 6.3.1: p = most / n, its upper bound p + 2.576 sqrt(p (1 - p) / n), and the
     * min-entropy -log2 of it; 4 bits a byte is p_u <= 1/16. Kept in integers: the bound's
     * second term is under 0.0013 here, so p <= 1/16 - 1/512 is enough */
    EMB_ASSERT_TRUE((uint64_t)most * 512u <= (uint64_t)RAW_TOTAL * 31u);
}
