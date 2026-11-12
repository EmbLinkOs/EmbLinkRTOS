/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The entropy class (SPEC-017 §11, SEC-012, A124): a read assembled from a driver's blocks,
 * through a fake source the test steers: a block that fails its health test ends the read
 * with none of its bytes, a timeout keeps what was read. And the board's own source, or its
 * honest absence: EMB_ENOTSUP, nothing in its place. */
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

EMB_TEST(entropy_read_assembles_blocks)
{
    uint8_t out[50];
    size_t got = 0u;
    unsigned i;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_entropy_read(&fake_dev, out, sizeof(out), &got, WAIT));
    EMB_ASSERT_EQ(got, sizeof(out));
    EMB_ASSERT_EQ(fake.blocks, 4u); /* 16, 16, 16 and 2 of the fourth */
    for (i = 0u; i < sizeof(out); i++) {
        EMB_ASSERT_TRUE(out[i] == (uint8_t)((((i / BLOCK) + 1u) << 4) | (i % BLOCK)));
    }
    EMB_ASSERT_OK(emb_drv_entropy_read(&fake_dev, out, 0u, NULL, WAIT)); /* nothing asked */
}

EMB_TEST(entropy_health_failure_delivers_none_of_its_block)
{
    uint8_t out[64];
    size_t got = 99u;
    EMB_TEST_REQ("DRV-001");
    ready();
    (void)memset(out, 0xEE, sizeof(out));
    fake.fail_at = 3u;
    EMB_ASSERT_STATUS(emb_drv_entropy_read(&fake_dev, out, sizeof(out), &got, WAIT), EMB_EHEALTH);
    EMB_ASSERT_EQ(got, 2u * BLOCK);            /* the two blocks before it */
    EMB_ASSERT_TRUE(out[2u * BLOCK] == 0xEEu); /* nothing of the third */
    EMB_ASSERT_OK(emb_drv_entropy_read(&fake_dev, out, sizeof(out), &got, WAIT)); /* and on */
}

EMB_TEST(entropy_timeout_keeps_what_was_read)
{
    uint8_t out[64];
    size_t got = 0u;
    EMB_TEST_REQ("DRV-001");
    ready();
    fake.stall_at = 2u;
    EMB_ASSERT_STATUS(emb_drv_entropy_read(&fake_dev, out, sizeof(out), &got, WAIT), EMB_ETIMEDOUT);
    EMB_ASSERT_EQ(got, BLOCK);
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
    EMB_ASSERT_FAULTS((void)emb_drv_entropy_board(NULL));
}
