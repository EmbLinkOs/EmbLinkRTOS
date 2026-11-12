/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* FLT-004's demonstration: the software watchdog over the board's watchdog. A thread that
 * hangs, while the test's own thread keeps checking in, stops the feeding; the board's
 * watchdog resets the part, and after the reset the crash record names the hung thread's
 * party and the watchdog says it caused the reset. Built where the board has a watchdog
 * device; the reset check needs a board that resumes a case after a reset. */
#include <emb/crash.h>

#include <emb_test.h>
#include <soc_devices.h>

#define WDT     EMB_SOC_WATCHDOG_DEVICE
#define HUNG_ID 7u
#define MAIN_ID 1u

static emb_wdog_party_t main_party;
static emb_wdog_party_t hung_party;
static volatile bool hung;

/* Checks in ten times, then hangs: suspended for good, as a deadlocked thread would be. */
static void hangs(void *arg)
{
    unsigned i;
    (void)arg;
    (void)emb_wdog_watch(&hung_party, HUNG_ID, 200u);
    for (i = 0u; i < 10u; i++) { /* bounded: ten */
        (void)emb_thread_sleep(EMB_MS(20u));
        (void)emb_wdog_checkin(&hung_party);
    }
    hung = true;
    (void)emb_thread_suspend(emb_thread_self());
}

static void hang_a_thread(void)
{
    emb_thread_attr_t attr;
    emb_thread_t t;
    unsigned i;
    EMB_ASSERT_OK(emb_wdog_watch(&main_party, MAIN_ID, 200u));
    EMB_ASSERT_OK(emb_wdog_start(WDT, 400u));
    emb_thread_attr_default(&attr);
    attr.priority = 2u; /* above the test's thread */
    attr.stack = emb_test_scratch_stack(&attr.stack_size);
    EMB_ASSERT_OK(emb_thread_init(emb_test_scratch_storage(), &attr, hangs, NULL, &t));
    EMB_ASSERT_OK(emb_thread_start(t));
    for (i = 0u; (i < 100u) && !hung; i++) { /* bounded: two seconds; it hangs after 200 ms */
        EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(20u)));
        EMB_ASSERT_OK(emb_wdog_checkin(&main_party));
    }
    EMB_ASSERT_TRUE(hung);
    emb_test_reset_expected();    /* fed until now: a reset before this fails the check */
    for (i = 0u; i < 150u; i++) { /* bounded: three seconds; the reset comes within 700 ms */
        (void)emb_thread_sleep(EMB_MS(20u));
        (void)emb_wdog_checkin(&main_party);
    }
}

EMB_TEST(wdog_board_hung_thread_resets_the_board)
{
    emb_crash_record_t rec;
    bool yes = false;
    EMB_TEST_REQ("FLT-004");
    EMB_ASSERT_RESETS(hang_a_thread());
    EMB_ASSERT_OK(emb_drv_wdt_caused_reset(WDT, &yes)); /* the boot after the reset */
    EMB_ASSERT_TRUE(yes);
#if CONFIG_EMB_CRASH_RECORD
    EMB_ASSERT_OK(emb_crash_record_get(&rec));
    EMB_ASSERT_EQ(rec.fault_class, (uint8_t)EMB_FAULT_WATCHDOG);
    EMB_ASSERT_EQ(rec.code, HUNG_ID); /* the hung thread's party, not the one on time */
    EMB_ASSERT_EQ(rec.reset_reason, EMB_CRASH_RESET_WATCHDOG);
    EMB_ASSERT_TRUE(rec.thread_index != EMB_CRASH_THREAD_NONE);
    EMB_ASSERT_TRUE(rec.thread_index != (uint16_t)emb_thread_index(emb_thread_self()));
    EMB_ASSERT_OK(emb_crash_record_clear());
#else
    (void)rec;
#endif
}
