/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_uart_loop.c
 * @brief   The UART class receiving on real hardware (SPEC-017 §4; DRV-004 to DRV-006).
 *
 * @details
 * A UART whose transmit pin is wired to its own receive pin, so every byte it sends comes back
 * through its receiver, its interrupt and the class's receive core. Built only where the bench has
 * the wire: EMB_TEST_UART_LOOP names the device (tests/conformance/CMakeLists.txt).
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <soc_devices.h>
#include <string.h>

#define LOOP EMB_TEST_UART_LOOP_DEV
#define RING 64u /* the device's rx_buffer, the binding's default */

static uint8_t out[1024];
static uint8_t in[1024];

static void pattern(size_t n, uint8_t seed)
{
    size_t i;
    for (i = 0u; i < n; i++) { /* bounded by n: every byte value, zeros included */
        out[i] = (uint8_t)((i * 37u) + seed);
    }
}

/* A case's read. A case that fails returns early, so the next case's drain() ends it. */
static emb_drv_uart_req_t rd;

/* Ends the last case's read if it was left waiting, and takes what the ring kept, so each
 * case starts on a quiet line. */
static void drain(void)
{
    size_t got = 1u;
    (void)emb_drv_cancel(LOOP, &rd.req); /* EMB_ESTATE when it had ended */
    while (got != 0u) {                  /* bounded: the ring is finite and the line quiet */
        (void)emb_drv_uart_read(LOOP, in, sizeof(in), EMB_DRV_UART_IDLE, EMB_NO_WAIT, &got);
    }
}

EMB_TEST(uart_loop_echo_full_duplex)
{
    size_t done = 0u;
    EMB_TEST_REQ("DRV-004", "DRV-005");
    EMB_ASSERT_TRUE(emb_device_ready(LOOP));
    drain();
    pattern(64u, 0x5Au);
    emb_drv_uart_req_read(&rd, in, 64u, 0u);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &rd)); /* the read waits on lane 1 */
    EMB_ASSERT_OK(emb_drv_uart_write(LOOP, out, 64u, EMB_TIMEOUT(EMB_MS(1000)), &done));
    EMB_ASSERT_EQ(done, 64u);
    emb_thread_sleep(EMB_MS(5));              /* the last byte's frame comes back */
    EMB_ASSERT_STATUS(rd.req.status, EMB_OK); /* it ended with the write's last byte */
    EMB_ASSERT_EQ(rd.req.done, 64u);
    EMB_ASSERT_TRUE(memcmp(in, out, 64u) == 0);
}

EMB_TEST(uart_loop_ring_keeps_what_arrives)
{
    size_t got = 0u;
    EMB_TEST_REQ("DRV-005");
    drain();
    pattern(10u, 0x11u);
    EMB_ASSERT_OK(emb_drv_uart_write(LOOP, out, 10u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
    emb_thread_sleep(EMB_MS(5)); /* the last byte's frame */
    EMB_ASSERT_OK(emb_drv_uart_read(LOOP, in, 10u, 0u, EMB_NO_WAIT, &got)); /* from the ring */
    EMB_ASSERT_EQ(got, 10u);
    EMB_ASSERT_TRUE(memcmp(in, out, 10u) == 0);
}

EMB_TEST(uart_loop_idle_line_ends_a_read)
{
    EMB_TEST_REQ("DRV-005");
    drain();
    pattern(5u, 0x22u);
    emb_drv_uart_req_read(&rd, in, 32u, EMB_DRV_UART_IDLE);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &rd));
    EMB_ASSERT_OK(emb_drv_uart_write(LOOP, out, 5u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
    emb_thread_sleep(EMB_MS(5));              /* five frames and the idle line after them */
    EMB_ASSERT_STATUS(rd.req.status, EMB_OK); /* the hardware's idle detection ended it */
    EMB_ASSERT_EQ(rd.req.done, 5u);
    EMB_ASSERT_TRUE(memcmp(in, out, 5u) == 0);
}

EMB_TEST(uart_loop_a_kilobyte_intact)
{
    size_t sent = 0u;
    EMB_TEST_REQ("DRV-004", "DRV-005");
    drain();
    pattern(sizeof(out), 0x03u);
    (void)memset(in, 0, sizeof(in));
    emb_drv_uart_req_read(&rd, in, sizeof(in), 0u);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &rd));
    EMB_ASSERT_OK(emb_drv_uart_write(LOOP, out, sizeof(out), EMB_TIMEOUT(EMB_MS(2000)), &sent));
    EMB_ASSERT_EQ(sent, sizeof(out));
    emb_thread_sleep(EMB_MS(5));
    EMB_ASSERT_STATUS(rd.req.status, EMB_OK);
    EMB_ASSERT_EQ(rd.req.done, sizeof(in));
    EMB_ASSERT_TRUE(memcmp(in, out, sizeof(in)) == 0);
}

EMB_TEST(uart_loop_break_arrives_as_a_framing_error)
{
    emb_drv_uart_req_t brk;
    EMB_TEST_REQ("DRV-004", "DRV-008");
    if ((emb_device_caps(LOOP) & EMB_DRV_UART_CAP_BREAK) == 0u) {
        emb_test_skip(EMB_TEST_STR("no break on this UART"));
        return;
    }
    drain();
    emb_drv_uart_req_read(&rd, in, 8u, 0u);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &rd));
    emb_drv_uart_req_break(&brk);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &brk));
    emb_thread_sleep(EMB_MS(5));
    /* a whole frame low with no stop bit: the receiver's framing error ends the read */
    EMB_ASSERT_STATUS(brk.req.status, EMB_OK);
    EMB_ASSERT_STATUS(rd.req.status, EMB_EFRAMING);
}

EMB_TEST(uart_loop_overrun_is_reported)
{
    size_t got = 0u;
    EMB_TEST_REQ("DRV-005", "DRV-008");
    drain();
    pattern(RING + 36u, 0x44u);
    EMB_ASSERT_OK(emb_drv_uart_write(LOOP, out, RING + 36u, EMB_TIMEOUT(EMB_MS(1000)), NULL));
    emb_thread_sleep(EMB_MS(5));
    /* no read was waiting: the ring kept 64 and lost the rest */
    EMB_ASSERT_STATUS(emb_drv_uart_read(LOOP, in, sizeof(in), 0u, EMB_NO_WAIT, &got), EMB_EOVERRUN);
    EMB_ASSERT_EQ(got, RING);
    EMB_ASSERT_TRUE(memcmp(in, out, RING) == 0);
}

#ifdef EMB_TEST_UART_LOOP_DMA_TX
/* The board gives the UART a DMA channel: writes of 16 bytes and more run by DMA, and one
 * canceled part-way has sent what it counts. */
static const emb_drv_dma_chan_t loop_dma_tx = EMB_TEST_UART_LOOP_DMA_TX;
static uint8_t probe;

static void probe_done(void *arg, emb_status_t st, uint32_t moved)
{
    (void)arg;
    (void)st;
    (void)moved;
}

EMB_TEST(uart_loop_writes_by_dma)
{
    emb_drv_uart_req_t w;
    emb_status_t st;
    emb_drv_dma_job_t job = {
        (uintptr_t)&probe, (uintptr_t)&probe, 1u, EMB_DRV_DMA_TO_PERIPH, 1u, 0u, 0u};
    size_t got = 0u;
    EMB_TEST_REQ("DRV-004", "DRV-006");
    drain();
    pattern(64u, 0x71u);
    emb_drv_uart_req_read(&rd, in, 64u, 0u);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &rd));
    emb_drv_uart_req_write(&w, out, 64u);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &w));
    st = emb_drv_dma_start(&loop_dma_tx, &job, probe_done, NULL);
    if (st == EMB_OK) {
        (void)emb_drv_dma_stop(&loop_dma_tx, NULL); /* free: the probe must not keep it */
    }
    EMB_ASSERT_STATUS(st, EMB_EBUSY);
    /* the transfer has the channel */ /* the write has the channel */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(20)));
    EMB_ASSERT_STATUS(w.req.status, EMB_OK);
    EMB_ASSERT_STATUS(rd.req.status, EMB_OK);
    EMB_ASSERT_TRUE(memcmp(in, out, 64u) == 0); /* every byte, back through the wire */

    drain();
    pattern(512u, 0x13u); /* some 44 ms at 115200 baud */
    emb_drv_uart_req_read(&rd, in, 512u, 0u);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &rd));
    emb_drv_uart_req_write(&w, out, 512u);
    EMB_ASSERT_OK(emb_drv_uart_submit(LOOP, &w));
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(10)));
    EMB_ASSERT_OK(emb_drv_cancel(LOOP, &w.req)); /* part-way */
    EMB_ASSERT_STATUS(w.req.status, EMB_ECANCELED);
    EMB_ASSERT_TRUE(w.req.done > 0u && w.req.done < 512u);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(10))); /* what went out arrives */
    (void)emb_drv_cancel(LOOP, &rd.req);
    got = (size_t)rd.req.done;
    EMB_ASSERT_TRUE(got >= w.req.done - 2u && got <= w.req.done); /* less what was in flight */
    EMB_ASSERT_TRUE(memcmp(in, out, got) == 0);
}
#endif
