/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The software watchdog (FLT-004, A121), over a fake hardware watchdog that counts its feeds:
 * it feeds while every party checks in within its window; one that misses stops the feeding
 * for good and is written into the crash record. The hardware's reset itself is
 * test_wdog_board's. */
#include <emb/crash.h>

#include <embk/crash.h>

#include <emb_test.h>
#include <string.h>

static volatile unsigned feeds;
static bool hw_started;
static emb_device_state_t wdt_state;

static emb_status_t fake_start(const emb_device_t *dev, uint32_t timeout_ms)
{
    (void)dev;
    if (hw_started) {
        return EMB_ESTATE;
    }
    if (timeout_ms > 10000u) {
        return EMB_EINVAL;
    }
    hw_started = true;
    return EMB_OK;
}

static void fake_feed(const emb_device_t *dev)
{
    (void)dev;
    feeds++;
}

static bool fake_caused(const emb_device_t *dev)
{
    (void)dev;
    return false;
}

static const emb_drv_wdt_api_t fake_api = {
    {NULL, NULL, NULL, NULL, NULL, 0u}, fake_start, fake_feed, fake_caused};
static const emb_device_t wdt_dev = {
    "wdt", &fake_api.dev, NULL, &wdt_state, NULL, NULL, EMB_DRV_CLASS_WATCHDOG, EMB_DEVICE_EARLY,
    0u};

static emb_wdog_party_t steady;
static emb_wdog_party_t stalls;

static void ready(void)
{
    if (!emb_device_ready(&wdt_dev)) {
        EMB_ASSERT_OK(emb_device_init(&wdt_dev));
    }
}

/* Sleeps @n times 10 ms, checking @a in each time, and @b too while @b_too. */
static void run(unsigned n, emb_wdog_party_t *a, emb_wdog_party_t *b, bool b_too)
{
    unsigned i;
    for (i = 0u; i < n; i++) { /* bounded: n */
        EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(10u)));
        EMB_ASSERT_OK(emb_wdog_checkin(a));
        if (b_too) {
            EMB_ASSERT_OK(emb_wdog_checkin(b));
        }
    }
}

EMB_TEST(wdog_refusals)
{
    emb_wdog_party_t idle;
    EMB_TEST_REQ("FLT-004");
    (void)memset(&idle, 0, sizeof(idle));
    ready();
    EMB_ASSERT_STATUS(emb_wdog_start(&wdt_dev, 3u), EMB_EINVAL); /* under 4 ms */
    EMB_ASSERT_STATUS(emb_wdog_watch(&idle, 9u, 0u), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_wdog_checkin(&idle), EMB_ESTATE); /* not watched */
    EMB_ASSERT_STATUS(emb_wdog_unwatch(&idle), EMB_ESTATE);
    EMB_ASSERT_OK(emb_wdog_watch(&idle, 9u, 100u));
    EMB_ASSERT_STATUS(emb_wdog_watch(&idle, 9u, 100u), EMB_ESTATE); /* once */
    EMB_ASSERT_OK(emb_wdog_checkin(&idle));
    EMB_ASSERT_OK(emb_wdog_unwatch(&idle)); /* gone before it could miss */
    EMB_ASSERT_FAULTS((void)emb_wdog_start(NULL, 100u));
    EMB_ASSERT_FAULTS((void)emb_wdog_watch(NULL, 1u, 100u));
    EMB_ASSERT_FAULTS((void)emb_wdog_checkin(NULL));
    EMB_ASSERT_FAULTS((void)emb_wdog_unwatch(NULL));
}

EMB_TEST(wdog_feeds_until_a_party_misses)
{
    unsigned before;
    emb_crash_record_t rec;
    EMB_TEST_REQ("FLT-004");
    ready();
    EMB_ASSERT_OK(emb_wdog_watch(&steady, 1u, 60u));
    EMB_ASSERT_OK(emb_wdog_watch(&stalls, 2u, 60u));
    EMB_ASSERT_OK(emb_wdog_start(&wdt_dev, 40u)); /* looks every 10 ms */
    EMB_ASSERT_STATUS(emb_wdog_start(&wdt_dev, 40u), EMB_ESTATE);
    run(20u, &steady, &stalls, true); /* 200 ms, both on time */
    EMB_ASSERT_TRUE(feeds >= 10u);
    run(15u, &steady, &stalls, false); /* 150 ms: the second misses its 60 */
    before = feeds;
    run(10u, &steady, &stalls, false);
    EMB_ASSERT_EQ(feeds, before); /* not fed again: the hardware would reset the part */
#if CONFIG_EMB_CRASH_RECORD
    embk_crash_record_boot(); /* as the next boot would */
    EMB_ASSERT_OK(emb_crash_record_get(&rec));
    EMB_ASSERT_EQ(rec.fault_class, (uint8_t)EMB_FAULT_WATCHDOG);
    EMB_ASSERT_EQ(rec.code, 2u); /* the party that missed, not the one on time */
    EMB_ASSERT_EQ(rec.reset_reason, EMB_CRASH_RESET_WATCHDOG);
    EMB_ASSERT_EQ(rec.thread_index, (uint16_t)emb_thread_index(emb_thread_self()));
    EMB_ASSERT_OK(emb_crash_record_clear());
#else
    (void)rec;
#endif
}
