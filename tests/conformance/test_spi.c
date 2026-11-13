/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_spi.c
 * @brief   The SPI controller class (SPEC-017 §5; DRV-004 to DRV-006).
 *
 * @details
 * Its requests and blocking wrapper, through a fake controller whose transfers end in a software
 * interrupt, each frame looped back as a wire from MOSI to MISO would.
 *
 * @author  teo1747
 * @date    2026-11-13 (created)
 */
#include <emb_test.h>
#include <string.h>

#define IRQ_DONE 2u /* software interrupt slot: the fake's "transfer complete" */
#define CS_LINE  1u /* the fake GPIO's line the targets' chip select is */

/* A GPIO controller that remembers its lines' levels and counts their edges. */
static uint32_t gpio_levels;
static unsigned cs_edges;
static emb_device_state_t gpio_state;

static emb_status_t gpio_configure(const emb_device_t *dev, uint32_t line, uint32_t flags)
{
    (void)dev;
    if ((flags & EMB_DRV_GPIO_INIT_HIGH) != 0u) {
        gpio_levels |= 1u << line;
    }
    return EMB_OK;
}

static void gpio_write(const emb_device_t *dev, uint32_t line, bool high)
{
    uint32_t was = gpio_levels;
    (void)dev;
    gpio_levels = high ? (gpio_levels | (1u << line)) : (gpio_levels & ~(1u << line));
    cs_edges += (was != gpio_levels) ? 1u : 0u;
}

static bool gpio_read(const emb_device_t *dev, uint32_t line)
{
    (void)dev;
    return ((gpio_levels >> line) & 1u) != 0u;
}

static const emb_drv_gpio_api_t gpio_api = {
    {NULL, NULL, NULL, NULL, NULL, 0u}, gpio_configure, gpio_write, gpio_read};
static const emb_device_t gpio_dev = {
    "gpio", &gpio_api.dev, NULL, &gpio_state, NULL, NULL, EMB_DRV_CLASS_GPIO, EMB_DEVICE_EARLY, 0u};

static bool cs_low(void)
{
    return ((gpio_levels >> CS_LINE) & 1u) == 0u;
}

typedef struct fake_spi {
    emb_drv_bus_t bus; /* the class's, first */
    emb_drv_spi_req_t *xfer;
    unsigned transfers;
    uint8_t stall; /* the "bus" never finishes: for cancel and timeout */
    uint8_t last_mode;
    uint8_t cs_at_start; /* the chip select was asserted when the frames began */
} fake_spi_t;

static fake_spi_t fake;
static emb_device_state_t spi_state;
static const emb_device_t spi_dev;

static emb_status_t fake_init(const emb_device_t *dev)
{
    return emb_drv_spi_bus_init(dev);
}

static emb_status_t fake_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_drv_spi_req_t *x = EMB_DRV_SPI_REQ(req);
    if (x->target->bits != 8u) {
        return EMB_ENOTSUP;
    }
    emb_drv_spi_begin(dev, x);
    fake.xfer = x;
    fake.last_mode = x->target->mode;
    fake.cs_at_start = cs_low() ? 1u : 0u;
    if (fake.stall == 0u) {
        emb_test_irq_raise(IRQ_DONE);
    }
    return EMB_EBUSY;
}

static void fake_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    fake.xfer = NULL;
    emb_drv_spi_end(dev, EMB_DRV_SPI_REQ(req));
    emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
}

/* The "bus": each frame sent comes back, ones where nothing was sent. */
EMB_TEST_ISR(fake_done_isr)
{
    emb_drv_spi_req_t *x = fake.xfer;
    size_t i;
    (void)emb_isr_arg_;
    if (x == NULL) {
        return;
    }
    fake.xfer = NULL;
    for (i = 0u; i < x->len; i++) { /* bounded by the transfer's length */
        if (x->rx != NULL) {
            x->rx[i] = (x->tx != NULL) ? x->tx[i] : 0xFFu;
        }
    }
    fake.transfers++;
    emb_drv_spi_end(&spi_dev, x);
    emb_drv_complete(&spi_dev, &x->req, EMB_OK, (uint32_t)x->len);
}

static const emb_device_api_t fake_api = {fake_init, fake_start, fake_stop, NULL, NULL, 0u};
static const emb_device_t spi_dev = {
    "spi", &fake_api, NULL, &spi_state, &fake, NULL, EMB_DRV_CLASS_SPI, EMB_DEVICE_KERNEL, 0u};
static const emb_device_api_t plain_api = {NULL, fake_start, NULL, NULL, NULL, 0u};
static emb_device_state_t plain_state;
static const emb_device_t plain_dev = {
    "plain", &plain_api, NULL, &plain_state, NULL, NULL, EMB_DRV_CLASS_NONE, EMB_DEVICE_KERNEL, 0u};

#define NO_CS         \
    {                 \
        NULL, 0u, 0u, \
        {             \
            0u, 0u    \
        }             \
    }
#define CS                      \
    {                           \
        &gpio_dev, CS_LINE, 1u, \
        {                       \
            0u, 0u              \
        }                       \
    } /* active low */

static const emb_drv_spi_target_t mode0 = {1000000u, EMB_DRV_SPI_MODE_0, 8u, 0u, 0u, NO_CS};
static const emb_drv_spi_target_t mode3 = {1000000u, EMB_DRV_SPI_MODE_3, 8u, 0u, 0u, NO_CS};
static const emb_drv_spi_target_t flash = {1000000u, EMB_DRV_SPI_MODE_0, 8u, 0u, 0u, CS};
static const emb_drv_spi_target_t sensor = {500000u, EMB_DRV_SPI_MODE_3, 8u, 0u, 0u, NO_CS};

static bool bus_live; /* the previous case's device left its bus mutex */

static void ready(void)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    if (bus_live) { /* each case initializes the device anew: its mutex first goes */
        EMB_ASSERT_OK(emb_mutex_destroy(fake.bus.lock));
    }
    spi_state = idle;
    (void)memset(&fake, 0, sizeof(fake));
    gpio_levels = 0u;
    cs_edges = 0u;
    EMB_ASSERT_OK(
        emb_drv_gpio_configure(&gpio_dev, CS_LINE, EMB_DRV_GPIO_OUTPUT | EMB_DRV_GPIO_INIT_HIGH));
    EMB_ASSERT_OK(emb_device_init(&spi_dev));
    bus_live = true;
    emb_test_irq_connect(IRQ_DONE, fake_done_isr, NULL);
}

EMB_TEST(spi_transfer_loops_back)
{
    static const uint8_t out[6] = {0x00u, 0x5Au, 0xA5u, 0xFFu, 0x01u, 0x80u};
    uint8_t in[6] = {0u};
    size_t done = 0u;
    EMB_TEST_REQ("DRV-004", "DRV-005");
    ready();
    EMB_ASSERT_OK(
        emb_drv_spi_transfer(&spi_dev, &mode3, out, in, sizeof(out), EMB_WAIT_FOREVER, &done));
    EMB_ASSERT_EQ(done, sizeof(out));
    EMB_ASSERT_TRUE(memcmp(in, out, sizeof(out)) == 0);
    EMB_ASSERT_EQ(fake.last_mode, EMB_DRV_SPI_MODE_3); /* the target's mode reached the driver */
}

EMB_TEST(spi_without_buffers)
{
    uint8_t in[4] = {0u};
    EMB_TEST_REQ("DRV-005");
    ready();
    EMB_ASSERT_OK(
        emb_drv_spi_transfer(&spi_dev, &mode0, NULL, in, sizeof(in), EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_TRUE(in[0] == 0xFFu && in[3] == 0xFFu); /* no transmit buffer: ones go out */
    EMB_ASSERT_OK(
        emb_drv_spi_transfer(&spi_dev, &mode0, in, NULL, sizeof(in), EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_OK(emb_drv_spi_transfer(&spi_dev, &mode0, NULL, NULL, 0u, EMB_NO_WAIT, NULL));
}

EMB_TEST(spi_transfers_run_in_order)
{
    emb_drv_spi_req_t a;
    emb_drv_spi_req_t b;
    uint8_t one = 1u;
    uint8_t two = 2u;
    uint8_t got[2] = {0u};
    EMB_TEST_REQ("DRV-004");
    ready();
    fake.stall = 1u;
    emb_drv_spi_req_transfer(&a, &mode0, &one, &got[0], 1u);
    emb_drv_spi_req_transfer(&b, &mode0, &two, &got[1], 1u);
    EMB_ASSERT_OK(emb_drv_spi_submit(&spi_dev, &a));
    EMB_ASSERT_OK(emb_drv_spi_submit(&spi_dev, &b)); /* one at a time: b waits */
    EMB_ASSERT_TRUE(fake.xfer == &a);
    fake.stall = 0u;
    emb_test_irq_raise(IRQ_DONE); /* a ends, b starts and ends */
    EMB_ASSERT_STATUS(a.req.status, EMB_OK);
    EMB_ASSERT_STATUS(b.req.status, EMB_OK);
    EMB_ASSERT_TRUE(got[0] == 1u && got[1] == 2u);
    EMB_ASSERT_EQ(fake.transfers, 2u);
}

EMB_TEST(spi_unsupported_and_stopped)
{
    static const emb_drv_spi_target_t wide = {1000000u, EMB_DRV_SPI_MODE_0, 16u, 0u, 0u, NO_CS};
    uint8_t in[2] = {0u};
    size_t done = 9u;
    EMB_TEST_REQ("DRV-006");
    ready();
    EMB_ASSERT_STATUS(emb_drv_spi_transfer(&spi_dev, &wide, NULL, in, 1u, EMB_NO_WAIT, NULL),
                      EMB_ENOTSUP); /* a frame size the controller does not do */
    fake.stall = 1u;
    EMB_ASSERT_STATUS(emb_drv_spi_transfer(&spi_dev, &mode0, NULL, in, sizeof(in),
                                           EMB_TIMEOUT(EMB_MS(10)), &done),
                      EMB_ETIMEDOUT); /* the driver stopped it */
    EMB_ASSERT_EQ(done, 0u);
    EMB_ASSERT_TRUE(fake.xfer == NULL);
}

EMB_TEST(spi_misuse_faults)
{
    static const emb_drv_spi_target_t no_clock = {0u, EMB_DRV_SPI_MODE_0, 8u, 0u, 0u, NO_CS};
    static const emb_drv_spi_target_t no_mode = {1000u, 4u, 8u, 0u, 0u, NO_CS};
    emb_drv_spi_req_t req;
    EMB_TEST_REQ("DRV-004");
    ready();
    emb_drv_spi_req_transfer(&req, &mode0, NULL, NULL, 0u);
    EMB_ASSERT_FAULTS((void)emb_drv_spi_submit(&plain_dev, &req)); /* not an SPI controller */
    EMB_ASSERT_FAULTS((void)emb_drv_spi_submit(&spi_dev, NULL));
    emb_drv_spi_req_transfer(&req, NULL, NULL, NULL, 0u);
    EMB_ASSERT_FAULTS((void)emb_drv_spi_submit(&spi_dev, &req)); /* no target */
    EMB_ASSERT_FAULTS(
        (void)emb_drv_spi_transfer(&spi_dev, &no_clock, NULL, NULL, 0u, EMB_NO_WAIT, NULL));
    EMB_ASSERT_FAULTS(
        (void)emb_drv_spi_transfer(&spi_dev, &no_mode, NULL, NULL, 0u, EMB_NO_WAIT, NULL));
    emb_drv_spi_req_transfer(NULL, &mode0, NULL, NULL, 0u); /* ignored */
}

EMB_TEST(spi_chip_select_around_each_transfer)
{
    uint8_t in[4] = {0u};
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_TRUE(!cs_low()); /* set up deasserted */
    EMB_ASSERT_OK(
        emb_drv_spi_transfer(&spi_dev, &flash, NULL, in, sizeof(in), EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_EQ(fake.cs_at_start, 1u); /* asserted while the frames ran */
    EMB_ASSERT_TRUE(!cs_low());          /* and released after */
    EMB_ASSERT_OK(
        emb_drv_spi_transfer(&spi_dev, &flash, NULL, in, sizeof(in), EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_EQ(cs_edges, 4u); /* down and up, twice */
    fake.stall = 1u;
    EMB_ASSERT_STATUS(
        emb_drv_spi_transfer(&spi_dev, &flash, NULL, in, sizeof(in), EMB_TIMEOUT(EMB_MS(5)), NULL),
        EMB_ETIMEDOUT);
    EMB_ASSERT_TRUE(!cs_low()); /* a stopped transfer releases it too */
}

EMB_TEST(spi_held_bus_keeps_the_chip_select)
{
    static const uint8_t cmd[4] = {0x03u, 0x00u, 0x10u, 0x00u};
    uint8_t in[8] = {0u};
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_OK(emb_drv_spi_acquire(&spi_dev, &flash, EMB_NO_WAIT));
    EMB_ASSERT_TRUE(cs_low());
    EMB_ASSERT_OK(
        emb_drv_spi_transfer(&spi_dev, &flash, cmd, NULL, sizeof(cmd), EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_TRUE(cs_low()); /* still asserted between the command and its data */
    EMB_ASSERT_OK(emb_drv_spi_acquire(&spi_dev, &flash, EMB_NO_WAIT)); /* again, nested */
    EMB_ASSERT_OK(
        emb_drv_spi_transfer(&spi_dev, &flash, NULL, in, sizeof(in), EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_OK(emb_drv_spi_release(&spi_dev));
    EMB_ASSERT_TRUE(cs_low()); /* one acquire left */
    EMB_ASSERT_OK(emb_drv_spi_release(&spi_dev));
    EMB_ASSERT_TRUE(!cs_low());
    EMB_ASSERT_EQ(cs_edges, 2u); /* one select for the whole exchange */
    EMB_ASSERT_EQ(fake.transfers, 2u);
}

EMB_TEST(spi_held_bus_refuses_another_target)
{
    emb_drv_spi_req_t req;
    uint8_t b = 0u;
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_OK(emb_drv_spi_acquire(&spi_dev, &flash, EMB_NO_WAIT));
    EMB_ASSERT_STATUS(emb_drv_spi_acquire(&spi_dev, &sensor, EMB_NO_WAIT), EMB_EBUSY);
    EMB_ASSERT_STATUS(emb_drv_spi_transfer(&spi_dev, &sensor, &b, NULL, 1u, EMB_NO_WAIT, NULL),
                      EMB_EBUSY);
    emb_drv_spi_req_transfer(&req, &sensor, &b, NULL, 1u);
    EMB_ASSERT_STATUS(emb_drv_spi_submit(&spi_dev, &req), EMB_EBUSY); /* from anywhere */
    EMB_ASSERT_OK(emb_drv_spi_release(&spi_dev));
    EMB_ASSERT_OK(emb_drv_spi_transfer(&spi_dev, &sensor, &b, NULL, 1u, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_STATUS(emb_drv_spi_release(&spi_dev), EMB_EPERM); /* nothing held */
}

static emb_status_t other_status;

static void other_thread_transfers(void *arg)
{
    uint8_t b = 0x5Au;
    other_status = emb_drv_spi_transfer(&spi_dev, &mode0, &b, NULL, 1u,
                                        EMB_TIMEOUT(EMB_TICKS((uint32_t)(uintptr_t)arg)), NULL);
    emb_test_mark(1);
}

EMB_TEST(spi_other_threads_wait_for_a_held_bus)
{
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_OK(emb_drv_spi_acquire(&spi_dev, &mode0, EMB_NO_WAIT));
    other_status = EMB_OK;
    (void)emb_test_thread(EMB_TEST_NAME("short"), 4u, 0u, other_thread_transfers, (void *)5);
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(10)));
    EMB_ASSERT_STATUS(other_status, EMB_ETIMEDOUT); /* it waited, and its timeout ran out */
    (void)emb_test_thread(EMB_TEST_NAME("long"), 4u, 0u, other_thread_transfers, (void *)100);
    EMB_ASSERT_EQ(fake.transfers, 0u);
    EMB_ASSERT_OK(emb_drv_spi_release(&spi_dev)); /* hands the bus over: it runs now */
    EMB_ASSERT_OK(emb_thread_sleep(EMB_TICKS(2)));
    EMB_ASSERT_OK(other_status);
    EMB_ASSERT_EQ(fake.transfers, 1u);
    EMB_ASSERT_MARKS(1, 1);
}
