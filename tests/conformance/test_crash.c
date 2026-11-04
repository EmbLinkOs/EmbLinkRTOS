/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * The crash record (SPEC-015 §4, OBS-016, A71 to A73): what a fault writes, what boot
 * validation accepts and refuses, get and clear. On the native port a forked child faults
 * for real and the parent, as the next boot, reads the host file it left. The record across
 * a real reset is test_crash_reset.
 */
#include "crash_print.h"

#include <emb/arch.h>
#include <emb/crash.h>

#include <embk/crash.h>

#include <emb_test.h>

#if CONFIG_EMB_CRASH_RECORD
static void write_sample(void)
{
    emb_fault_info_t info;
    emb_irq_key_t key;
    info.fault_class = EMB_FAULT_API_LIFECYCLE;
    info.argument = 2u;
    info.code = 0x0102u;
    info.api_id = EMB_API_ID_SEM_GIVE;
    info.thread_index = 3u;
    info.context = 0u;
    info.address = 0x1234u;
    info.pc = 0x5678u;
    info.sp = 0x7ABCu;
    info.where = NULL;
    key = emb_arch_irq_lock(); /* as on the fault path */
    embk_crash_record_write(&info, EMB_CRASH_RESET_REBOOT);
    emb_arch_irq_unlock(key);
}

static uint8_t *retained(void)
{
    uint16_t size = 0u;
    return emb_arch_retained(&size);
}

EMB_TEST(crash_record_round_trip)
{
    emb_crash_record_t r;
    uint16_t i;
    uint16_t id_bytes = (EMBK_CRASH_VERSION == EMB_CRASH_VERSION_SMALL) ? 4u : 20u;
    EMB_TEST_REQ("OBS-016", "API-017");
    write_sample();
    embk_crash_record_boot();
    EMB_ASSERT_OK(emb_crash_record_get(&r));
    EMB_ASSERT_EQ(r.version, EMBK_CRASH_VERSION);
    EMB_ASSERT_EQ(r.length, EMBK_CRASH_LENGTH);
    EMB_ASSERT_TRUE(r.raw[0] == 'E' && r.raw[1] == 'M' && r.raw[2] == 'B' && r.raw[3] == 'C');
    for (i = 0u; i < id_bytes; i++) { /* the image's own identity */
        EMB_ASSERT_EQ(r.build_id[i], EMB_FLASH_READ_U8(&emb_build_id[i]));
    }
    EMB_ASSERT_EQ(r.reset_reason, EMB_CRASH_RESET_REBOOT);
    EMB_ASSERT_EQ(r.fault_class, EMB_FAULT_API_LIFECYCLE);
    EMB_ASSERT_EQ(r.argument, 2u);
    EMB_ASSERT_EQ(r.code, 0x0102u);
    EMB_ASSERT_EQ(r.api_id, EMB_API_ID_SEM_GIVE);
    EMB_ASSERT_EQ(r.thread_index, 3u);
    EMB_ASSERT_EQ(r.address, 0x1234u);
    EMB_ASSERT_EQ(r.pc, 0x5678u);
    EMB_ASSERT_EQ(r.sp, 0x7ABCu);
    EMB_ASSERT_TRUE(r.uptime_ticks <= (uint64_t)emb_time_now().ticks);
#if EMBK_CRASH_VERSION == EMB_CRASH_VERSION_FULL
    EMB_ASSERT_EQ(r.fw_version[0], CONFIG_EMB_FW_VERSION[0]);
#endif
    crash_print(&r);
    EMB_ASSERT_OK(emb_crash_record_clear());
    EMB_ASSERT_STATUS(emb_crash_record_get(&r), EMB_ENOENT);
    embk_crash_record_boot(); /* and after a reset as well */
    EMB_ASSERT_STATUS(emb_crash_record_get(&r), EMB_ENOENT);
}

EMB_TEST(crash_record_corruption_is_ignored)
{
    static const uint8_t offsets[] = {0u, 4u, 6u, 17u};
    emb_crash_record_t r;
    uint8_t i;
    EMB_TEST_REQ("OBS-016");
    for (i = 0u; i < (uint8_t)sizeof(offsets); i++) { /* magic, version, length, a field */
        write_sample();
        retained()[offsets[i]] ^= 0x01u;
        embk_crash_record_boot();
        EMB_ASSERT_STATUS(emb_crash_record_get(&r), EMB_ENOENT);
    }
    write_sample();
    retained()[EMBK_CRASH_LENGTH - 1u] ^= 0x80u; /* the CRC itself */
    embk_crash_record_boot();
    EMB_ASSERT_STATUS(emb_crash_record_get(&r), EMB_ENOENT);
    EMB_ASSERT_OK(emb_crash_record_clear());
}

EMB_TEST(crash_record_get_refuses_null)
{
    EMB_TEST_REQ("OBS-016", "API-015");
    EMB_ASSERT_MISUSE(emb_crash_record_get(NULL), EMB_EINVAL);
}

#if CONFIG_EMB_ARCH_NATIVE && EMB_TEST_HAS_FORK && CONFIG_EMB_CHECKED && CONFIG_EMB_SEM
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>

/* The fault really happens, in a child process: the record must be in the host file before
 * the child is gone, and the parent's boot validation must find it there. */
EMB_TEST(crash_record_from_a_real_fault_reaches_the_next_boot)
{
    char path[] = "/tmp/emb-crash-XXXXXX";
    emb_sem_t none = EMB_HANDLE_NULL;
    emb_crash_record_t r;
    int fd = mkstemp(path);
    EMB_TEST_REQ("OBS-016", "API-017", "KRN-IRQ-030");
    EMB_ASSERT_TRUE(fd >= 0);
    (void)close(fd);
    EMB_ASSERT_TRUE(setenv("EMB_NATIVE_CRASH_FILE", path, 1) == 0);

    EMB_ASSERT_FAULTS(emb_sem_give(none)); /* a misuse: the API and the argument */
    embk_crash_record_boot();
    EMB_ASSERT_OK(emb_crash_record_get(&r));
    EMB_ASSERT_EQ(r.fault_class, EMB_FAULT_API_HANDLE);
    EMB_ASSERT_EQ(r.api_id, EMB_API_ID_SEM_GIVE);
    EMB_ASSERT_EQ(r.argument, 1u);
    EMB_ASSERT_TRUE(r.thread_index != EMB_CRASH_THREAD_NONE);
    EMB_ASSERT_EQ(r.reset_reason,
                  CONFIG_EMB_FAULT_ACTION_REBOOT ? EMB_CRASH_RESET_REBOOT : EMB_CRASH_RESET_HALT);

    EMB_ASSERT_FAULTS(emb_fault(EMB_FAULT_CHECK, 0x0BADu, "test_crash")); /* an application fault */
    embk_crash_record_boot();
    EMB_ASSERT_OK(emb_crash_record_get(&r));
    EMB_ASSERT_EQ(r.fault_class, EMB_FAULT_CHECK);
    EMB_ASSERT_EQ(r.code, 0x0BADu);
    EMB_ASSERT_EQ(r.api_id, EMB_API_ID_NONE);
    crash_print(&r);

    EMB_ASSERT_OK(emb_crash_record_clear());
    embk_crash_record_boot();
    EMB_ASSERT_STATUS(emb_crash_record_get(&r), EMB_ENOENT); /* cleared in the file too */
    EMB_ASSERT_TRUE(unsetenv("EMB_NATIVE_CRASH_FILE") == 0);
    (void)remove(path);
}
#endif
#endif
