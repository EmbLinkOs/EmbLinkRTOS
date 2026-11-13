/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_uart.c
 * @brief   The UART class (SPEC-017 §4; DRV-004 to DRV-007).
 *
 * @details
 * Its requests, wrappers and polled calls, and the receive core drivers share, through a fake
 * loopback UART whose transmitter is a software interrupt that feeds each byte back to its own
 * receiver.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <string.h>

#define IRQ_TX 2u /* software interrupt slot: the fake's "transmitter" */
#define RING   16u

typedef struct fake_uart {
    emb_drv_uart_rx_t rx;
    emb_drv_uart_req_t *tx; /* the write the "transmitter" is sending */
    uint8_t loopback;       /* the transmitter feeds the receiver */
    uint8_t poll_line[8];   /* the polled calls' own line */
    size_t poll_len;
    unsigned breaks;
} fake_uart_t;

static uint8_t ring[RING];
static fake_uart_t fake;
static emb_device_state_t uart_state;
static const emb_device_t uart_dev;

static emb_status_t fake_init(const emb_device_t *dev)
{
    (void)dev;
    emb_drv_uart_rx_init(&fake.rx, ring, sizeof(ring));
    return EMB_OK;
}

static emb_status_t fake_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    if (req->lane == EMB_DRV_UART_LANE_RX) {
        return emb_drv_uart_rx_start(dev, &fake.rx, EMB_DRV_UART_REQ(req));
    }
    if (EMB_DRV_UART_REQ(req)->op == EMB_DRV_UART_OP_BREAK) {
        fake.breaks++;
        return EMB_OK;
    }
    fake.tx = EMB_DRV_UART_REQ(req);
    emb_test_irq_raise(IRQ_TX);
    return EMB_EBUSY; /* the "transmitter" ends it */
}

static void fake_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    if (req->lane == EMB_DRV_UART_LANE_RX) {
        emb_drv_uart_rx_stop(dev, &fake.rx, EMB_DRV_UART_REQ(req));
        return;
    }
    fake.tx = NULL;
    emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
}

/* The transmitter: sends the whole write, each byte looped back to the receiver. */
EMB_TEST_ISR(fake_tx_isr)
{
    emb_drv_uart_req_t *w = fake.tx;
    (void)emb_isr_arg_;
    if (w == NULL) {
        return;
    }
    fake.tx = NULL;
    while ((size_t)w->req.done < w->len) { /* bounded by the write's length */
        uint8_t b = w->tx[w->req.done];
        w->req.done++;
        if (fake.loopback != 0u) {
            emb_drv_uart_rx_byte(&uart_dev, &fake.rx, b);
        }
    }
    emb_drv_complete(&uart_dev, &w->req, EMB_OK, w->req.done);
}

static emb_status_t fake_poll_write(const emb_device_t *dev, const uint8_t *buf, size_t len)
{
    (void)dev;
    if (len > sizeof(fake.poll_line)) {
        return EMB_EOVERFLOW;
    }
    (void)memcpy(fake.poll_line, buf, len);
    fake.poll_len = len;
    return EMB_OK;
}

static size_t fake_poll_read(const emb_device_t *dev, uint8_t *buf, size_t len)
{
    size_t n = (len < fake.poll_len) ? len : fake.poll_len;
    (void)dev;
    (void)memcpy(buf, fake.poll_line, n);
    fake.poll_len = 0u;
    return n;
}

static const emb_drv_uart_api_t fake_api = {
    {fake_init, fake_start, fake_stop, NULL, NULL, EMB_DRV_UART_CAP_IDLE_DETECT},
    fake_poll_write,
    fake_poll_read,
};
static const emb_device_t uart_dev = {
    "uart", &fake_api.dev, NULL, &uart_state, &fake, NULL, EMB_DRV_CLASS_UART, EMB_DEVICE_KERNEL,
    0u};

/* not a UART: the class refuses it */
static const emb_device_api_t plain_api = {NULL, fake_start, NULL, NULL, NULL, 0u};
static emb_device_state_t plain_state;
static const emb_device_t plain_dev = {
    "plain", &plain_api, NULL, &plain_state, NULL, NULL, EMB_DRV_CLASS_NONE, EMB_DEVICE_KERNEL, 0u};

static void ready(void)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    uart_state = idle;
    (void)memset(&fake, 0, sizeof(fake));
    fake.loopback = 1u;
    EMB_ASSERT_OK(emb_device_init(&uart_dev));
    emb_test_irq_connect(IRQ_TX, fake_tx_isr, NULL);
}

EMB_TEST(uart_read_and_write_run_at_once)
{
    emb_drv_uart_req_t rd;
    uint8_t in[5] = {0u};
    size_t done = 0u;
    EMB_TEST_REQ("DRV-004", "DRV-005");
    ready();
    emb_drv_uart_req_read(&rd, in, sizeof(in), 0u);
    EMB_ASSERT_OK(emb_drv_uart_submit(&uart_dev, &rd)); /* waits on lane 1 */
    EMB_ASSERT_STATUS(rd.req.status, EMB_EBUSY);
    EMB_ASSERT_OK(emb_drv_uart_write(&uart_dev, "hello", 5u, EMB_WAIT_FOREVER, &done));
    EMB_ASSERT_EQ(done, 5u); /* the write ran beside the pending read, and filled it */
    EMB_ASSERT_STATUS(rd.req.status, EMB_OK);
    EMB_ASSERT_EQ(rd.req.done, 5u);
    EMB_ASSERT_TRUE(memcmp(in, "hello", 5u) == 0);
}

EMB_TEST(uart_ring_keeps_bytes_no_read_waits_for)
{
    uint8_t in[8] = {0u};
    size_t got = 0u;
    EMB_TEST_REQ("DRV-005");
    ready();
    EMB_ASSERT_OK(emb_drv_uart_write(&uart_dev, "abcd", 4u, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_EQ(fake.rx.count, 4u);
    EMB_ASSERT_OK(emb_drv_uart_read(&uart_dev, in, 3u, 0u, EMB_NO_WAIT, &got)); /* from the ring */
    EMB_ASSERT_TRUE(got == 3u && memcmp(in, "abc", 3u) == 0);
    EMB_ASSERT_OK(emb_drv_uart_read(&uart_dev, in, 1u, 0u, EMB_NO_WAIT, &got));
    EMB_ASSERT_TRUE(got == 1u && in[0] == (uint8_t)'d');
    EMB_ASSERT_EQ(fake.rx.count, 0u);
}

EMB_TEST(uart_idle_ends_a_read_early)
{
    emb_drv_uart_req_t rd;
    uint8_t in[10] = {0u};
    EMB_TEST_REQ("DRV-005");
    ready();
    emb_drv_uart_req_read(&rd, in, sizeof(in), EMB_DRV_UART_IDLE);
    EMB_ASSERT_OK(emb_drv_uart_submit(&uart_dev, &rd));
    emb_drv_uart_rx_idle(&uart_dev, &fake.rx); /* idle before any byte: the read waits on */
    EMB_ASSERT_STATUS(rd.req.status, EMB_EBUSY);
    EMB_ASSERT_OK(emb_drv_uart_write(&uart_dev, "xyz", 3u, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_STATUS(rd.req.status, EMB_EBUSY); /* 3 of 10 */
    emb_drv_uart_rx_idle(&uart_dev, &fake.rx);
    EMB_ASSERT_STATUS(rd.req.status, EMB_OK);
    EMB_ASSERT_TRUE(rd.req.done == 3u && memcmp(in, "xyz", 3u) == 0);
    /* bytes already waiting: an idle read takes them at once */
    EMB_ASSERT_OK(emb_drv_uart_write(&uart_dev, "pq", 2u, EMB_WAIT_FOREVER, NULL));
    emb_drv_uart_req_read(&rd, in, sizeof(in), EMB_DRV_UART_IDLE);
    EMB_ASSERT_OK(emb_drv_uart_submit(&uart_dev, &rd));
    EMB_ASSERT_TRUE(rd.req.status == EMB_OK && rd.req.done == 2u);
}

EMB_TEST(uart_overrun_and_damaged_bytes)
{
    static uint8_t many[RING + 4u];
    uint8_t in[RING + 4u];
    emb_drv_uart_req_t rd;
    size_t got = 0u;
    EMB_TEST_REQ("DRV-005", "DRV-008");
    ready();
    (void)memset(many, 0x5A, sizeof(many));
    EMB_ASSERT_OK(emb_drv_uart_write(&uart_dev, many, sizeof(many), EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_EQ(fake.rx.lost, 4u); /* the ring holds 16: 4 were lost */
    EMB_ASSERT_STATUS(emb_drv_uart_read(&uart_dev, in, sizeof(in), 0u, EMB_NO_WAIT, &got),
                      EMB_EOVERRUN);
    EMB_ASSERT_EQ(got, RING); /* what the ring kept, and the loss reported once */
    EMB_ASSERT_OK(emb_drv_uart_write(&uart_dev, "ok", 2u, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_OK(emb_drv_uart_read(&uart_dev, in, 2u, 0u, EMB_NO_WAIT, &got));

    emb_drv_uart_req_read(&rd, in, 4u, 0u);
    EMB_ASSERT_OK(emb_drv_uart_submit(&uart_dev, &rd));
    emb_drv_uart_rx_byte(&uart_dev, &fake.rx, 0x31u);
    emb_drv_uart_rx_error(&uart_dev, &fake.rx, EMB_EFRAMING); /* the second byte arrived bad */
    EMB_ASSERT_STATUS(rd.req.status, EMB_EFRAMING);
    EMB_ASSERT_EQ(rd.req.done, 1u);
    emb_drv_uart_rx_error(&uart_dev, &fake.rx, EMB_EPARITY); /* no read: dropped */
    EMB_ASSERT_EQ(fake.rx.count, 0u);
}

EMB_TEST(uart_read_times_out_with_what_it_got)
{
    uint8_t in[4] = {0u};
    size_t got = 7u;
    EMB_TEST_REQ("DRV-006");
    ready();
    EMB_ASSERT_STATUS(
        emb_drv_uart_read(&uart_dev, in, sizeof(in), 0u, EMB_TIMEOUT(EMB_MS(10)), &got),
        EMB_ETIMEDOUT);
    EMB_ASSERT_EQ(got, 0u);
    EMB_ASSERT_TRUE(fake.rx.active == NULL); /* stopped, so a later byte goes to the ring */
    fake.loopback = 0u; /* a write the receiver does not hear: it completes on its own */
    EMB_ASSERT_OK(emb_drv_uart_write(&uart_dev, "zz", 2u, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_EQ(fake.rx.count, 0u);
}

EMB_TEST(uart_polled_calls_and_capabilities)
{
    uint8_t in[4] = {0u};
    size_t got = 0u;
    EMB_TEST_REQ("DRV-007");
    ready();
    EMB_ASSERT_OK(emb_drv_uart_poll_write(&uart_dev, "hi", 2u));
    EMB_ASSERT_OK(emb_drv_uart_poll_read(&uart_dev, in, sizeof(in), &got));
    EMB_ASSERT_TRUE(got == 2u && memcmp(in, "hi", 2u) == 0);
    EMB_ASSERT_EQ(emb_device_caps(&uart_dev), EMB_DRV_UART_CAP_IDLE_DETECT);
    EMB_ASSERT_EQ(emb_device_caps(NULL), 0u);
    EMB_ASSERT_STATUS(emb_drv_uart_poll_write(&plain_dev, "hi", 2u), EMB_EINVAL); /* not a UART */
    EMB_ASSERT_STATUS(emb_drv_uart_poll_read(&uart_dev, in, sizeof(in), NULL), EMB_EINVAL);
}

EMB_TEST(uart_break_runs_after_the_writes)
{
    emb_drv_uart_req_t brk;
    EMB_TEST_REQ("DRV-004");
    ready();
    emb_drv_uart_req_break(&brk);
    EMB_ASSERT_EQ(brk.req.lane, EMB_DRV_UART_LANE_TX);
    EMB_ASSERT_OK(emb_drv_uart_submit(&uart_dev, &brk));
    EMB_ASSERT_STATUS(brk.req.status, EMB_OK);
    EMB_ASSERT_EQ(fake.breaks, 1u);
    emb_drv_uart_req_break(NULL); /* ignored */
}

EMB_TEST(uart_misuse_faults)
{
    emb_drv_uart_req_t req;
    EMB_TEST_REQ("DRV-004");
    ready();
    emb_drv_uart_req_write(&req, "a", 1u);
    EMB_ASSERT_FAULTS((void)emb_drv_uart_submit(&plain_dev, &req)); /* not a UART */
    EMB_ASSERT_FAULTS((void)emb_drv_uart_submit(&uart_dev, NULL));
    emb_drv_uart_req_write(&req, NULL, 1u);
    EMB_ASSERT_FAULTS((void)emb_drv_uart_submit(&uart_dev, &req)); /* a length and no bytes */
    EMB_ASSERT_FAULTS((void)emb_drv_uart_write(&plain_dev, "a", 1u, EMB_NO_WAIT, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_uart_read(&uart_dev, NULL, 1u, 0u, EMB_NO_WAIT, NULL));
    emb_drv_uart_req_write(NULL, "a", 1u); /* ignored */
    emb_drv_uart_req_read(NULL, NULL, 0u, 0u);
}
