/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The board's RTC (SPEC-017 §10, A123): set and read back; kept in step with the kernel's clock,
 * two crystals, over two seconds; its alarm on time, and canceled; its backup registers; and its
 * time across a reset, which then seeds the wall clock. The reset check needs a board that
 * resumes a case after a reset. Built where the board has an RTC (EMB_SOC_RTC_DEVICE). */
#include <emb/arch.h>
#if CONFIG_EMB_WALLCLOCK
#include <emb/wallclock.h>
#endif

#include <emb_test.h>
#include <soc_devices.h>

#define RTC     EMB_SOC_RTC_DEVICE
#define T0      INT64_C(1794297600500000000) /* 2026-11-10T08:00:00.5Z */
#define MS      INT64_C(1000000)
#define S       INT64_C(1000000000)
#define TICK_NS ((int64_t)CONFIG_EMB_TICK_NS)
#define SLACK   (10 * MS) /* the RTC's resolution, 1/256 s at worst, and a tick each way */
#define MAGIC   0x5EEDu

static int64_t mono_ns(void)
{
    return (int64_t)emb_time_now().ticks * TICK_NS;
}

static volatile int64_t fired_at;

static void on_alarm(void *arg)
{
    (void)arg;
    fired_at = mono_ns();
}

EMB_TEST(rtc_board_set_read_and_keep_time)
{
    int64_t r0 = 0;
    int64_t r1 = 0;
    int64_t m0;
    int64_t m1;
    EMB_TEST_REQ("DRV-001");
    EMB_ASSERT_TRUE(emb_device_ready(RTC));
    m0 = mono_ns();
    EMB_ASSERT_OK(emb_drv_rtc_set(RTC, T0));
    EMB_ASSERT_OK(emb_drv_rtc_get(RTC, &r0));
    m1 = mono_ns();
    /* the RTC holds T0 from the call on: the read is T0 plus what set and get took */
    emb_test_logf(EMB_TEST_STR("  set and get took %ld ms; the RTC read %ld ms past the time set"),
                  (long)((m1 - m0) / MS), (long)((r0 - T0) / MS));
    EMB_ASSERT_TRUE((r0 >= T0 - SLACK) && (r0 <= T0 + (m1 - m0) + SLACK));
    m0 = m1;
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(2000u)));
    EMB_ASSERT_OK(emb_drv_rtc_get(RTC, &r1));
    m1 = mono_ns();
    emb_test_logf(EMB_TEST_STR("  over %ld ms of the kernel's clock the RTC moved %ld ms"),
                  (long)((m1 - m0) / MS), (long)((r1 - r0) / MS));
    EMB_ASSERT_TRUE(((r1 - r0) - (m1 - m0) <= SLACK) && ((m1 - m0) - (r1 - r0) <= SLACK));
    EMB_ASSERT_STATUS(emb_drv_rtc_set(RTC, -1), EMB_EINVAL);
    if ((emb_device_caps(RTC) & EMB_DRV_RTC_CAP_CALENDAR) != 0u) {
        EMB_ASSERT_STATUS(emb_drv_rtc_set(RTC, INT64_C(4200000000) * S), EMB_EINVAL); /* 2103 */
    }
}

EMB_TEST(rtc_board_alarm_on_time)
{
    int64_t now = 0;
    int64_t m0;
    unsigned i;
    EMB_TEST_REQ("DRV-001");
    EMB_ASSERT_OK(emb_drv_rtc_get(RTC, &now));
    fired_at = 0;
    m0 = mono_ns();
    EMB_ASSERT_OK(emb_drv_rtc_alarm_set(RTC, now + (300 * MS), on_alarm, NULL));
    for (i = 0u; (i < 100u) && (fired_at == 0); i++) { /* bounded: a second */
        EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(10u)));
    }
    EMB_ASSERT_TRUE(fired_at != 0);
    emb_test_logf(EMB_TEST_STR("  an alarm 300 ms ahead fired after %ld ms"),
                  (long)((fired_at - m0) / MS));
    EMB_ASSERT_TRUE((fired_at - m0 >= (300 * MS) - SLACK) && (fired_at - m0 <= (300 * MS) + SLACK));
    /* canceled: nothing fires */
    EMB_ASSERT_OK(emb_drv_rtc_get(RTC, &now));
    fired_at = 0;
    EMB_ASSERT_OK(emb_drv_rtc_alarm_set(RTC, now + (200 * MS), on_alarm, NULL));
    EMB_ASSERT_OK(emb_drv_rtc_alarm_cancel(RTC));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(400u)));
    EMB_ASSERT_TRUE(fired_at == 0);
    EMB_ASSERT_OK(emb_drv_rtc_get(RTC, &now));
    EMB_ASSERT_STATUS(emb_drv_rtc_alarm_set(RTC, now - S, on_alarm, NULL), EMB_EINVAL); /* past */
    if ((emb_device_caps(RTC) & EMB_DRV_RTC_CAP_CALENDAR) != 0u) {
        EMB_ASSERT_STATUS(emb_drv_rtc_alarm_set(RTC, now + (30 * 86400 * S), on_alarm, NULL),
                          EMB_EINVAL); /* past the month the alarm compares */
    }
}

EMB_TEST(rtc_board_backup_registers)
{
    uint32_t v = 0u;
    EMB_TEST_REQ("DRV-001");
    if ((emb_device_caps(RTC) & EMB_DRV_RTC_CAP_BACKUP) == 0u) {
        EMB_ASSERT_STATUS(emb_drv_rtc_backup_read(RTC, 0u, &v), EMB_ENOTSUP);
        return;
    }
    EMB_ASSERT_OK(emb_drv_rtc_backup_write(RTC, 19u, 0xA5C3F00Fu));
    EMB_ASSERT_OK(emb_drv_rtc_backup_read(RTC, 19u, &v));
    EMB_ASSERT_EQ(v, 0xA5C3F00Fu);
    EMB_ASSERT_STATUS(emb_drv_rtc_backup_read(RTC, 20u, &v), EMB_EINVAL);
}

/* The statement the reset check runs: the time set, then the part reset by software. */
static void set_then_reset(void)
{
    EMB_ASSERT_OK(emb_drv_rtc_set(RTC, T0));
    if ((emb_device_caps(RTC) & EMB_DRV_RTC_CAP_BACKUP) != 0u) {
        EMB_ASSERT_OK(emb_drv_rtc_backup_write(RTC, 0u, MAGIC));
    }
    emb_test_reset_expected();
    emb_arch_system_reset();
}

EMB_TEST(rtc_board_keeps_its_time_across_a_reset)
{
    int64_t ns = 0;
    uint32_t v = 0u;
    EMB_TEST_REQ("DRV-001", "KRN-TIM-031");
    EMB_ASSERT_RESETS(set_then_reset());
    EMB_ASSERT_OK(emb_drv_rtc_get(RTC, &ns)); /* the boot after the reset */
    emb_test_logf(EMB_TEST_STR("  after the reset the RTC read %ld ms past the time set"),
                  (long)((ns - T0) / MS));
    EMB_ASSERT_TRUE((ns >= T0 - SLACK) && (ns < T0 + (60 * S)));
    if ((emb_device_caps(RTC) & EMB_DRV_RTC_CAP_BACKUP) != 0u) {
        EMB_ASSERT_OK(emb_drv_rtc_backup_read(RTC, 0u, &v));
        EMB_ASSERT_EQ(v, MAGIC);
    }
#if CONFIG_EMB_WALLCLOCK
    {
        int64_t w = 0;
        EMB_ASSERT_OK(emb_wallclock_get(&w)); /* seeded from the RTC at boot */
        EMB_ASSERT_TRUE((w - ns <= SLACK) && (ns - w <= SLACK));
    }
#endif
}
