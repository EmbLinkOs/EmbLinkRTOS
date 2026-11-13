/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_entropy.c
 * @brief   The entropy class (SPEC-017 §11, SEC-012, A124).
 *
 * @details
 * Reads assembled from a driver's blocks, through a fake source the test steers: raw, and
 * conditioned (every 32 bytes the SHA-256 of 64 raw ones); a block that fails its health test ends
 * the read with none of its bytes, a timeout keeps what was read. And the board's own source, or
 * its honest absence: EMB_ENOTSUP, nothing in its place.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <embk/sha256.h>

#include <emb_test.h>
#include <string.h>

#define BLOCK 16u

typedef struct fake_entropy {
    emb_drv_bus_t lock; /* the class's, first */
    unsigned blocks;    /* blocks given so far */
    unsigned fail_at;   /* the block, counted from 1, that fails its health test; 0 for none */
    unsigned stall_at;  /* the block that never comes: the read times out; 0 for none */
} fake_entropy_t;

static fake_entropy_t fake;
static emb_device_state_t fake_state;

static emb_status_t fake_init(const emb_device_t *dev)
{
    return emb_drv_entropy_lock_init(dev);
}

static emb_status_t fake_block(const emb_device_t *dev, uint8_t *buf, emb_timeout_t timeout)
{
    unsigned i;
    (void)dev;
    fake.blocks++;
    if (fake.blocks == fake.stall_at) {
        (void)emb_thread_sleep(EMB_MS(5u));
        (void)timeout;
        return EMB_ETIMEDOUT;
    }
    for (i = 0u; i < BLOCK; i++) {
        buf[i] = (uint8_t)((fake.blocks << 4) | i); /* which block, which byte */
    }
    return (fake.blocks == fake.fail_at) ? EMB_EHEALTH : EMB_OK;
}

static const emb_drv_entropy_api_t fake_api = {
    {fake_init, NULL, NULL, NULL, NULL, EMB_DRV_ENTROPY_CAP_HEALTH_TEST},
    fake_block,
    BLOCK,
    {0u, 0u, 0u}};
static const emb_device_t fake_dev = {
    "entropy-fake",        &fake_api.dev,    NULL, &fake_state, &fake, NULL,
    EMB_DRV_CLASS_ENTROPY, EMB_DEVICE_EARLY, 0u};

#define WAIT EMB_TIMEOUT(EMB_MS(100u))

static void ready(void)
{
    fake.blocks = 0u;
    fake.fail_at = 0u;
    fake.stall_at = 0u;
    if (!emb_device_ready(&fake_dev)) {
        EMB_ASSERT_OK(emb_device_init(&fake_dev));
    }
}

/* The fake's raw stream: block b (from 1), byte i. */
static uint8_t raw_byte(unsigned at)
{
    return (uint8_t)((((at / BLOCK) + 1u) << 4) | (at % BLOCK));
}

/* FIPS 180-4's examples (the NIST CSRC example values): the conditioning function itself. */
EMB_TEST(entropy_conditioning_is_sha256)
{
    static const char abc[] = "abc";
    static const char two[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    static const uint8_t want_abc[32] = {0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                                         0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                                         0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                                         0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad};
    static const uint8_t want_two[32] = {0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8,
                                         0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
                                         0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67,
                                         0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1};
    static const uint8_t want_empty[32] = {0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
                                           0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
                                           0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
                                           0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55};
    embk_sha256_t sha;
    uint8_t d[EMBK_SHA256_SIZE];
    EMB_TEST_REQ("DRV-001");
    embk_sha256_start(&sha);
    embk_sha256_add(&sha, (const uint8_t *)abc, sizeof(abc) - 1u);
    embk_sha256_end(&sha, d);
    EMB_ASSERT_TRUE(memcmp(d, want_abc, sizeof(d)) == 0);
    embk_sha256_start(&sha); /* 56 bytes: the padding takes a second block */
    embk_sha256_add(&sha, (const uint8_t *)two, sizeof(two) - 1u);
    embk_sha256_end(&sha, d);
    EMB_ASSERT_TRUE(memcmp(d, want_two, sizeof(d)) == 0);
    embk_sha256_start(&sha);
    embk_sha256_end(&sha, d);
    EMB_ASSERT_TRUE(memcmp(d, want_empty, sizeof(d)) == 0);
}

EMB_TEST(entropy_read_conditions_twice_the_bytes)
{
    uint8_t out[40];
    uint8_t raw[64];
    uint8_t want[EMBK_SHA256_SIZE];
    embk_sha256_t sha;
    size_t got = 0u;
    unsigned d;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_entropy_read(&fake_dev, out, sizeof(out), &got, WAIT));
    EMB_ASSERT_EQ(got, sizeof(out));
    EMB_ASSERT_EQ(fake.blocks, 8u); /* 64 raw bytes for each of the two digests */
    for (d = 0u; d < 2u; d++) {
        unsigned i;
        for (i = 0u; i < sizeof(raw); i++) {
            raw[i] = raw_byte((64u * d) + i);
        }
        embk_sha256_start(&sha);
        embk_sha256_add(&sha, raw, sizeof(raw));
        embk_sha256_end(&sha, want);
        EMB_ASSERT_TRUE(memcmp(&out[32u * d], want, (d == 0u) ? 32u : 8u) == 0);
    }
}

EMB_TEST(entropy_read_raw_assembles_blocks)
{
    uint8_t out[50];
    size_t got = 0u;
    unsigned i;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_entropy_read_raw(&fake_dev, out, sizeof(out), &got, WAIT));
    EMB_ASSERT_EQ(got, sizeof(out));
    EMB_ASSERT_EQ(fake.blocks, 4u); /* 16, 16, 16 and 2 of the fourth */
    for (i = 0u; i < sizeof(out); i++) {
        EMB_ASSERT_TRUE(out[i] == raw_byte(i));
    }
    EMB_ASSERT_OK(emb_drv_entropy_read_raw(&fake_dev, out, 0u, NULL, WAIT)); /* nothing asked */
}

EMB_TEST(entropy_health_failure_delivers_none_of_its_block)
{
    uint8_t out[64];
    size_t got = 99u;
    EMB_TEST_REQ("DRV-001");
    ready();
    (void)memset(out, 0xEE, sizeof(out));
    fake.fail_at = 3u;
    EMB_ASSERT_STATUS(emb_drv_entropy_read_raw(&fake_dev, out, sizeof(out), &got, WAIT),
                      EMB_EHEALTH);
    EMB_ASSERT_EQ(got, 2u * BLOCK);            /* the two blocks before it */
    EMB_ASSERT_TRUE(out[2u * BLOCK] == 0xEEu); /* nothing of the third */
    /* conditioned: the sixth block fails among the second digest's raw bytes, so only the
     * first digest is out */
    fake.fail_at = fake.blocks + 6u;
    (void)memset(out, 0xEE, sizeof(out));
    EMB_ASSERT_STATUS(emb_drv_entropy_read(&fake_dev, out, sizeof(out), &got, WAIT), EMB_EHEALTH);
    EMB_ASSERT_EQ(got, 32u);
    EMB_ASSERT_TRUE(out[32] == 0xEEu);
    EMB_ASSERT_OK(emb_drv_entropy_read(&fake_dev, out, sizeof(out), &got, WAIT)); /* and on */
}

EMB_TEST(entropy_timeout_keeps_what_was_read)
{
    uint8_t out[64];
    size_t got = 0u;
    EMB_TEST_REQ("DRV-001");
    ready();
    fake.stall_at = 2u;
    EMB_ASSERT_STATUS(emb_drv_entropy_read_raw(&fake_dev, out, sizeof(out), &got, WAIT),
                      EMB_ETIMEDOUT);
    EMB_ASSERT_EQ(got, BLOCK);
    fake.stall_at = fake.blocks + 6u; /* conditioned: among the second digest's raw bytes */
    EMB_ASSERT_STATUS(emb_drv_entropy_read(&fake_dev, out, sizeof(out), &got, WAIT), EMB_ETIMEDOUT);
    EMB_ASSERT_EQ(got, 32u);
}

EMB_TEST(entropy_board_source_or_its_absence)
{
    const emb_device_t *dev = &fake_dev;
    uint8_t out[64];
    size_t got = 0u;
    unsigned i;
    unsigned zeros = 0u;
    EMB_TEST_REQ("SEC-012");
    if (emb_drv_entropy_board(&dev) == EMB_ENOTSUP) {
        EMB_ASSERT_TRUE(dev == NULL); /* no source, and nothing in its place */
        return;
    }
    EMB_ASSERT_TRUE(dev != NULL);
    EMB_ASSERT_OK(emb_drv_entropy_read(dev, out, sizeof(out), &got, EMB_TIMEOUT(EMB_MS(1000u))));
    EMB_ASSERT_EQ(got, sizeof(out));
    for (i = 0u; i < sizeof(out); i++) {
        zeros += (out[i] == 0u) ? 1u : 0u;
    }
    EMB_ASSERT_TRUE(zeros < 8u); /* 64 bytes of a real source are not mostly zero */
}

EMB_TEST(entropy_misuse_faults)
{
    uint8_t b = 0u;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_FAULTS((void)emb_drv_entropy_read(NULL, &b, 1u, NULL, WAIT));
    EMB_ASSERT_FAULTS((void)emb_drv_entropy_read(&fake_dev, NULL, 1u, NULL, WAIT));
    EMB_ASSERT_FAULTS((void)emb_drv_entropy_read_raw(NULL, &b, 1u, NULL, WAIT));
    EMB_ASSERT_FAULTS((void)emb_drv_entropy_board(NULL));
}
