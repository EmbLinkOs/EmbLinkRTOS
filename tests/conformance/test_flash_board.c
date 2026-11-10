/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The board's flash (SPEC-017 §9, A122): its storage partition erased, programmed and read back
 * three times over, and the erase timed by the processor's cycle counter against the kernel's
 * clock, which caught up with the time interrupts were held off or the CPU stalled. Built where
 * the board has a storage partition (EMB_FLASH_AREA_STORAGE); Cortex-M boards. */
#include <emb_cortex_m.h>
#include <emb_test.h>
#include <soc_devices.h>

#define FA     EMB_FLASH_AREA_STORAGE
#define CHUNK  256u
#define ROUNDS 3u
#define WAIT   EMB_TIMEOUT(EMB_MS(100u))

/* A page from flash: the RP2350's driver copies it to RAM before XIP goes off. */
static const uint8_t in_flash[CHUNK] = {0xC3u, 0x3Cu, 0xA5u, 0x5Au, 0x0Fu, 0xF0u, 0x00u, 0x11u};

static uint8_t pattern(uint32_t at, unsigned round)
{
    return (uint8_t)((at * 7u) + (at >> 8) + (round * 31u));
}

EMB_TEST(flash_board_erase_keeps_the_clock)
{
    uint32_t c0;
    uint32_t c1;
    emb_instant_t t0;
    emb_instant_t t1;
    uint32_t by_cycles;
    uint32_t by_clock;
    uint32_t step = (uint32_t)CONFIG_EMB_CM_CPU_HZ / 1000u;
    EMB_TEST_REQ("DRV-001", "DRV-003");
    EMB_ASSERT_TRUE(emb_device_ready(FA->dev));
    c0 = emb_cm_cycle_count();
    t0 = emb_time_now();
    EMB_ASSERT_OK(emb_flash_area_erase(FA, 0u, FA->size, WAIT));
    c1 = emb_cm_cycle_count();
    t1 = emb_time_now();
    by_clock = (uint32_t)((t1.ticks - t0.ticks) * (emb_tick_t)CONFIG_EMB_TICK_NS / 1000000u);
    if (c1 == c0) {
        emb_test_skip(EMB_TEST_STR("no cycle counter to time it by (an emulator)"));
        return;
    }
    by_cycles = (c1 - c0) / step;
    emb_test_logf(EMB_TEST_STR("  erase of %lu KiB: %lu ms by the cycles, %lu ms by the clock"),
                  (unsigned long)(FA->size / 1024u), (unsigned long)by_cycles,
                  (unsigned long)by_clock);
    /* within a tick either way, and the tick the erase started in */
    EMB_ASSERT_TRUE((by_clock + 2u >= by_cycles) && (by_clock <= by_cycles + 2u));
}

EMB_TEST(flash_board_program_and_read_back)
{
    uint8_t buf[CHUNK];
    unsigned round;
    EMB_TEST_REQ("DRV-001", "DRV-003");
    for (round = 0u; round < ROUNDS; round++) { /* bounded: three rounds */
        uint32_t at;
        EMB_ASSERT_OK(emb_flash_area_erase(FA, 0u, FA->size, WAIT));
        for (at = 0u; at < FA->size; at += CHUNK) { /* bounded: the partition */
            uint32_t i;
            for (i = 0u; i < CHUNK; i++) {
                buf[i] = pattern(at + i, round);
            }
            EMB_ASSERT_OK(emb_flash_area_program(FA, at, buf, CHUNK, WAIT));
        }
        for (at = 0u; at < FA->size; at += CHUNK) {
            uint32_t i;
            bool same = true;
            EMB_ASSERT_OK(emb_flash_area_read(FA, at, buf, CHUNK, WAIT));
            for (i = 0u; i < CHUNK; i++) {
                same = same && (buf[i] == pattern(at + i, round));
            }
            EMB_ASSERT_TRUE(same);
        }
    }
    /* the bytes from flash itself, then the partition left erased */
    EMB_ASSERT_OK(emb_flash_area_erase(FA, 0u, FA->size, WAIT));
    EMB_ASSERT_OK(emb_flash_area_program(FA, 0u, in_flash, CHUNK, WAIT));
    EMB_ASSERT_OK(emb_flash_area_read(FA, 0u, buf, 8u, WAIT));
    EMB_ASSERT_TRUE((buf[0] == 0xC3u) && (buf[7] == 0x11u));
    EMB_ASSERT_OK(emb_flash_area_erase(FA, 0u, FA->size, WAIT));
}
