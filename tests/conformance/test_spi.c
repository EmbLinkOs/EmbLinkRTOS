/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The SPI controller class (SPEC-017 §5; DRV-004 to DRV-006): its requests and blocking
 * wrapper, through a fake controller whose transfers end in a software interrupt, each frame
 * looped back as a wire from MOSI to MISO would. */
#include <emb_test.h>
#include <string.h>

#define IRQ_DONE 2u /* software interrupt slot: the fake's "transfer complete" */

typedef struct fake_spi {
    emb_drv_spi_req_t *xfer;
    unsigned transfers;
    uint8_t stall; /* the "bus" never finishes: for cancel and timeout */
    uint8_t last_mode;
} fake_spi_t;

static fake_spi_t fake;
static emb_device_state_t spi_state;
static const emb_device_t spi_dev;

static emb_status_t fake_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_drv_spi_req_t *x = EMB_DRV_SPI_REQ(req);
    (void)dev;
    if (x->target->bits != 8u) {
        return EMB_ENOTSUP;
    }
    fake.xfer = x;
    fake.last_mode = x->target->mode;
    if (fake.stall == 0u) {
        emb_test_irq_raise(IRQ_DONE);
    }
    return EMB_EBUSY;
}

static void fake_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    fake.xfer = NULL;
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
    emb_drv_complete(&spi_dev, &x->req, EMB_OK, (uint32_t)x->len);
}

static const emb_device_api_t fake_api = {NULL, fake_start, fake_stop, NULL, NULL, 0u};
static const emb_device_t spi_dev = {
    "spi", &fake_api, NULL, &spi_state, &fake, NULL, EMB_DRV_CLASS_SPI, EMB_DEVICE_KERNEL, 0u};
static const emb_device_api_t plain_api = {NULL, fake_start, NULL, NULL, NULL, 0u};
static emb_device_state_t plain_state;
static const emb_device_t plain_dev = {
    "plain", &plain_api, NULL, &plain_state, NULL, NULL, EMB_DRV_CLASS_NONE, EMB_DEVICE_KERNEL, 0u};

static const emb_drv_spi_target_t mode0 = {1000000u, EMB_DRV_SPI_MODE_0, 8u, 0u, 0u};
static const emb_drv_spi_target_t mode3 = {1000000u, EMB_DRV_SPI_MODE_3, 8u, 0u, 0u};

static void ready(void)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    spi_state = idle;
    (void)memset(&fake, 0, sizeof(fake));
    EMB_ASSERT_OK(emb_device_init(&spi_dev));
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
    static const emb_drv_spi_target_t wide = {1000000u, EMB_DRV_SPI_MODE_0, 16u, 0u, 0u};
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
    static const emb_drv_spi_target_t no_clock = {0u, EMB_DRV_SPI_MODE_0, 8u, 0u, 0u};
    static const emb_drv_spi_target_t no_mode = {1000u, 4u, 8u, 0u, 0u};
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
