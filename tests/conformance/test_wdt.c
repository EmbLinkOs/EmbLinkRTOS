/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The watchdog class (SPEC-017 §8; FLT-004): its checks over a driver's operations, through a
 * fake watchdog that counts its feeds. */
#include <emb_test.h>
#include <string.h>

typedef struct fake_wdt {
    uint32_t timeout_ms;
    unsigned feeds;
    bool started;
    bool caused;
} fake_wdt_t;

static fake_wdt_t fake;
static emb_device_state_t wdt_state;

static emb_status_t fake_start(const emb_device_t *dev, uint32_t timeout_ms)
{
    (void)dev;
    if (fake.started) {
        return EMB_ESTATE;
    }
    if (timeout_ms > 10000u) {
        return EMB_EINVAL; /* past what it counts */
    }
    fake.timeout_ms = timeout_ms;
    fake.started = true;
    return EMB_OK;
}

static void fake_feed(const emb_device_t *dev)
{
    (void)dev;
    fake.feeds++;
}

static bool fake_caused(const emb_device_t *dev)
{
    (void)dev;
    return fake.caused;
}

static const emb_drv_wdt_api_t fake_api = {
    {NULL, NULL, NULL, NULL, NULL, 0u}, fake_start, fake_feed, fake_caused};
static const emb_device_t wdt_dev = {
    "wdt", &fake_api.dev, NULL, &wdt_state, NULL, NULL, EMB_DRV_CLASS_WATCHDOG, EMB_DEVICE_EARLY,
    0u};
static const emb_device_api_t plain_api = {NULL, NULL, NULL, NULL, NULL, 0u};
static emb_device_state_t plain_state;
static const emb_device_t plain_dev = {
    "plain", &plain_api, NULL, &plain_state, NULL, NULL, EMB_DRV_CLASS_NONE, EMB_DEVICE_EARLY, 0u};

static void ready(void)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    wdt_state = idle;
    (void)memset(&fake, 0, sizeof(fake));
    EMB_ASSERT_OK(emb_device_init(&wdt_dev));
}

EMB_TEST(wdt_start_feed_and_cause)
{
    bool yes = true;
    EMB_TEST_REQ("FLT-004");
    ready();
    EMB_ASSERT_OK(emb_drv_wdt_caused_reset(&wdt_dev, &yes));
    EMB_ASSERT_TRUE(!yes);
    EMB_ASSERT_STATUS(emb_drv_wdt_start(&wdt_dev, 0u), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_drv_wdt_start(&wdt_dev, 60000u), EMB_EINVAL); /* the driver's limit */
    EMB_ASSERT_OK(emb_drv_wdt_start(&wdt_dev, 250u));
    EMB_ASSERT_EQ(fake.timeout_ms, 250u);
    EMB_ASSERT_STATUS(emb_drv_wdt_start(&wdt_dev, 250u), EMB_ESTATE); /* once */
    EMB_ASSERT_OK(emb_drv_wdt_feed(&wdt_dev));
    EMB_ASSERT_OK(emb_drv_wdt_feed(&wdt_dev));
    EMB_ASSERT_EQ(fake.feeds, 2u);
    fake.caused = true;
    EMB_ASSERT_OK(emb_drv_wdt_caused_reset(&wdt_dev, &yes));
    EMB_ASSERT_TRUE(yes);
}

EMB_TEST(wdt_not_ready_and_misuse)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    bool yes = false;
    EMB_TEST_REQ("FLT-004");
    ready();
    wdt_state = idle;
    EMB_ASSERT_STATUS(emb_drv_wdt_start(&wdt_dev, 100u), EMB_ESTATE);
    EMB_ASSERT_STATUS(emb_drv_wdt_feed(&wdt_dev), EMB_ESTATE);
    EMB_ASSERT_STATUS(emb_drv_wdt_caused_reset(&wdt_dev, &yes), EMB_ESTATE);
    EMB_ASSERT_FAULTS((void)emb_drv_wdt_start(&plain_dev, 100u)); /* not a watchdog */
    EMB_ASSERT_FAULTS((void)emb_drv_wdt_feed(NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_wdt_caused_reset(&wdt_dev, NULL));
}
