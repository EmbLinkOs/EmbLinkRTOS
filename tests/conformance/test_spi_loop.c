/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The SPI class on a real controller (SPEC-017 §5; DRV-004 to DRV-006), its MOSI looped to
 * its MISO: by a wire on the bench and in EmbSIM, or inside the controller where it has a
 * loopback mode and the emulator models it (QEMU's PL022). Built where one of these exists:
 * EMB_TEST_SPI_LOOP names the device (tests/conformance/CMakeLists.txt). */
#include <emb_test.h>
#include <soc_devices.h>
#include <string.h>

#define LOOP EMB_TEST_SPI_LOOP_DEV
#ifdef EMB_TEST_SPI_LOOP_INTERNAL
#define LOOP_FLAGS EMB_DRV_SPI_LOOPBACK
#else
#define LOOP_FLAGS 0u
#endif

static uint8_t out[256];
static uint8_t in[256];

static void pattern(size_t n, uint8_t seed)
{
    size_t i;
    for (i = 0u; i < n; i++) { /* bounded by n: every byte value, zeros included */
        out[i] = (uint8_t)((i * 73u) + seed);
    }
    (void)memset(in, 0, sizeof(in));
}

EMB_TEST(spi_loop_frames_come_back_intact)
{
    static const emb_drv_spi_target_t t = {1000000u, EMB_DRV_SPI_MODE_0, 8u, LOOP_FLAGS, 0u};
    size_t done = 0u;
    EMB_TEST_REQ("DRV-004", "DRV-005");
    EMB_ASSERT_TRUE(emb_device_ready(LOOP));
    pattern(sizeof(out), 0x3Cu);
    EMB_ASSERT_OK(
        emb_drv_spi_transfer(LOOP, &t, out, in, sizeof(out), EMB_TIMEOUT(EMB_MS(1000)), &done));
    EMB_ASSERT_EQ(done, sizeof(out));
    EMB_ASSERT_TRUE(memcmp(in, out, sizeof(out)) == 0);
}

EMB_TEST(spi_loop_every_mode)
{
    uint8_t mode;
    EMB_TEST_REQ("DRV-004");
    for (mode = EMB_DRV_SPI_MODE_0; mode <= EMB_DRV_SPI_MODE_3; mode++) { /* the four modes */
        const emb_drv_spi_target_t t = {2000000u, mode, 8u, LOOP_FLAGS, 0u};
        pattern(16u, mode);
        EMB_ASSERT_OK(
            emb_drv_spi_transfer(LOOP, &t, out, in, 16u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
        EMB_ASSERT_TRUE(memcmp(in, out, 16u) == 0);
    }
}

/* 200 kHz: within every controller here (the F446's SPI2 reaches 45 MHz / 256 = 176 kHz). */
EMB_TEST(spi_loop_slow_and_fast_clocks)
{
    static const emb_drv_spi_target_t slow = {200000u, EMB_DRV_SPI_MODE_0, 8u, LOOP_FLAGS, 0u};
    static const emb_drv_spi_target_t fast = {8000000u, EMB_DRV_SPI_MODE_0, 8u, LOOP_FLAGS, 0u};
    EMB_TEST_REQ("DRV-004");
    pattern(32u, 0x11u);
    EMB_ASSERT_OK(emb_drv_spi_transfer(LOOP, &slow, out, in, 32u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
    EMB_ASSERT_TRUE(memcmp(in, out, 32u) == 0);
    pattern(32u, 0x22u);
    EMB_ASSERT_OK(emb_drv_spi_transfer(LOOP, &fast, out, in, 32u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
    EMB_ASSERT_TRUE(memcmp(in, out, 32u) == 0);
}

EMB_TEST(spi_loop_refuses_a_clock_too_slow)
{
    static const emb_drv_spi_target_t crawl = {1u, EMB_DRV_SPI_MODE_0, 8u, LOOP_FLAGS, 0u};
    static const emb_drv_spi_target_t t = {1000000u, EMB_DRV_SPI_MODE_0, 8u, LOOP_FLAGS, 0u};
    size_t done = 9u;
    EMB_TEST_REQ("DRV-006");
    pattern(8u, 0x44u);
    EMB_ASSERT_STATUS(
        emb_drv_spi_transfer(LOOP, &crawl, out, in, 8u, EMB_TIMEOUT(EMB_MS(1000)), &done),
        EMB_ENOTSUP); /* the rate is a ceiling: no divider reaches 1 Hz */
    EMB_ASSERT_EQ(done, 0u);
    EMB_ASSERT_OK(emb_drv_spi_transfer(LOOP, &t, out, in, 8u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
    EMB_ASSERT_TRUE(memcmp(in, out, 8u) == 0); /* the controller is still usable */
}

/* 256 frames at 200 kHz take some 10 ms; a 2 ms timeout stops the transfer part-way, after
 * whole frames. An emulator that moves frames at once finishes it first: either way the frames
 * counted came back intact and the controller goes on. */
EMB_TEST(spi_loop_timeout_stops_after_whole_frames)
{
    static const emb_drv_spi_target_t slow = {200000u, EMB_DRV_SPI_MODE_0, 8u, LOOP_FLAGS, 0u};
    static const emb_drv_spi_target_t t = {1000000u, EMB_DRV_SPI_MODE_0, 8u, LOOP_FLAGS, 0u};
    size_t done = 0u;
    emb_status_t st;
    EMB_TEST_REQ("DRV-006");
    pattern(sizeof(out), 0x5Au);
    st = emb_drv_spi_transfer(LOOP, &slow, out, in, sizeof(out), EMB_TIMEOUT(EMB_MS(2)), &done);
    if (st == EMB_OK) {
        EMB_ASSERT_EQ(done, sizeof(out));
    } else {
        EMB_ASSERT_STATUS(st, EMB_ETIMEDOUT);
        EMB_ASSERT_TRUE(done > 0u && done < sizeof(out));
    }
    EMB_ASSERT_TRUE(memcmp(in, out, done) == 0);
    pattern(16u, 0x66u);
    EMB_ASSERT_OK(emb_drv_spi_transfer(LOOP, &t, out, in, 16u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
    EMB_ASSERT_TRUE(memcmp(in, out, 16u) == 0); /* nothing left over from the stopped one */
}

EMB_TEST(spi_loop_no_transmit_buffer_sends_ones)
{
    static const emb_drv_spi_target_t t = {1000000u, EMB_DRV_SPI_MODE_0, 8u, LOOP_FLAGS, 0u};
    size_t i;
    EMB_TEST_REQ("DRV-005");
    (void)memset(in, 0, sizeof(in));
    EMB_ASSERT_OK(emb_drv_spi_transfer(LOOP, &t, NULL, in, 16u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
    for (i = 0u; i < 16u; i++) { /* bounded: 16 frames */
        EMB_ASSERT_EQ(in[i], 0xFFu);
    }
}
