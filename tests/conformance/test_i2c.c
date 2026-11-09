/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The I2C class (SPEC-017 §6; DRV-004 to DRV-006): its requests and blocking wrappers, through
 * a fake controller. As a controller its "bus" holds one device, a register file at 0x50
 * that a write points into and a read reads from; as a target it answers a "remote
 * controller", a software interrupt that writes and reads this one's own address. */
#include <emb_test.h>
#include <string.h>

#define IRQ_DONE   2u /* software interrupt slot: the fake's transaction ends */
#define IRQ_REMOTE 3u /* the remote controller's transaction to this one */
#define DEVICE     0x50u
#define OWN        0x42u

typedef struct fake_i2c {
    emb_drv_bus_t bus;         /* the class's, first */
    emb_drv_i2c_req_t *xfer;   /* the transaction in progress */
    emb_drv_i2c_req_t *listen; /* the listen waiting */
    uint8_t regs[16];
    uint8_t ptr;
    uint8_t stall; /* the transaction never ends: for timeouts */
    /* the remote controller's next transaction: what it writes, then how much it reads */
    uint8_t remote_wr[8];
    size_t remote_wr_len;
    size_t remote_rd_len;
    uint8_t remote_got[8];
    uint8_t remote_addr;
    uint8_t remote_acked; /* the address was acknowledged */
} fake_i2c_t;

static fake_i2c_t fake;
static emb_device_state_t i2c_state;
static const emb_device_t i2c_dev;

static emb_status_t fake_init(const emb_device_t *dev)
{
    return emb_drv_i2c_bus_init(dev);
}

static emb_status_t fake_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_drv_i2c_req_t *x = EMB_DRV_I2C_REQ(req);
    (void)dev;
    if (req->lane == EMB_DRV_I2C_LANE_TARGET) {
        fake.listen = x; /* the address is acknowledged from now on */
        return EMB_EBUSY;
    }
    if (x->target->hz > EMB_DRV_I2C_FAST) {
        return EMB_ENOTSUP; /* no fast mode plus */
    }
    fake.xfer = x;
    if (fake.stall == 0u) {
        emb_test_irq_raise(IRQ_DONE);
    }
    return EMB_EBUSY;
}

static void fake_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    if (req->lane == EMB_DRV_I2C_LANE_TARGET) {
        fake.listen = NULL;
    } else {
        fake.xfer = NULL; /* a stop condition after the byte in progress */
    }
    emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
}

/* The bus, for this controller's transaction: the register file at DEVICE. */
EMB_TEST_ISR(fake_done_isr)
{
    emb_drv_i2c_req_t *x = fake.xfer;
    size_t s;
    size_t i;
    uint32_t moved = 0u;
    (void)emb_isr_arg_;
    if (x == NULL) {
        return;
    }
    fake.xfer = NULL;
    if (x->target->address != DEVICE) {
        emb_drv_complete(&i2c_dev, &x->req, EMB_ENACK, 0u); /* nobody answers */
        return;
    }
    for (s = 0u; s < x->count; s++) { /* bounded by the segments */
        const emb_drv_i2c_seg_t *g = &x->segs[s];
        for (i = 0u; i < g->len; i++) { /* bounded by the segment's length */
            if (g->wr != NULL) {
                if (s == 0u && i == 0u) {
                    fake.ptr = (uint8_t)(g->wr[0] & 0x0Fu); /* the first byte points */
                } else {
                    fake.regs[fake.ptr] = g->wr[i];
                    fake.ptr = (uint8_t)((fake.ptr + 1u) & 0x0Fu);
                }
            } else {
                g->rd[i] = fake.regs[fake.ptr];
                fake.ptr = (uint8_t)((fake.ptr + 1u) & 0x0Fu);
            }
            moved++;
        }
    }
    emb_drv_complete(&i2c_dev, &x->req, EMB_OK, moved);
}

/* The remote controller: writes, then reads after a repeated start, to remote_addr. */
EMB_TEST_ISR(fake_remote_isr)
{
    emb_drv_i2c_req_t *l = fake.listen;
    size_t i;
    (void)emb_isr_arg_;
    fake.remote_acked = 0u;
    if (l == NULL || l->own != fake.remote_addr) {
        return; /* not acknowledged */
    }
    fake.listen = NULL;
    fake.remote_acked = 1u;
    for (i = 0u; i < fake.remote_wr_len; i++) { /* bounded by the remote's write */
        if ((size_t)l->req.done >= l->rx_len) {
            break; /* not acknowledged: the remote stops writing */
        }
        l->rx[l->req.done] = fake.remote_wr[i];
        l->req.done++;
    }
    for (i = 0u; i < fake.remote_rd_len; i++) { /* bounded by the remote's read */
        fake.remote_got[i] = (l->sent < l->tx_len) ? l->tx[l->sent] : 0xFFu;
        l->sent++;
    }
    emb_drv_complete(&i2c_dev, &l->req, EMB_OK, l->req.done);
}

#define FAKE_CAPS (EMB_DRV_I2C_CAP_TARGET | EMB_DRV_I2C_CAP_FAST_MODE | EMB_DRV_I2C_CAP_TARGET_NACK)
static const emb_device_api_t fake_api = {fake_init, fake_start, fake_stop, NULL, NULL, FAKE_CAPS};
static const emb_device_t i2c_dev = {
    "i2c", &fake_api, NULL, &i2c_state, &fake, NULL, EMB_DRV_CLASS_I2C, EMB_DEVICE_KERNEL, 0u};
static const emb_device_api_t plain_api = {NULL, fake_start, NULL, NULL, NULL, 0u};
static emb_device_state_t plain_state;
static const emb_device_t plain_dev = {
    "plain", &plain_api, NULL, &plain_state, NULL, NULL, EMB_DRV_CLASS_NONE, EMB_DEVICE_KERNEL, 0u};

static const emb_drv_i2c_target_t eeprom = {EMB_DRV_I2C_STANDARD, DEVICE, {0u, 0u}};
static const emb_drv_i2c_target_t absent = {EMB_DRV_I2C_STANDARD, 0x51u, {0u, 0u}};
static const emb_drv_i2c_target_t too_fast = {EMB_DRV_I2C_FAST_PLUS, DEVICE, {0u, 0u}};

static bool bus_live; /* the previous case's device left its bus mutex */

static void ready(void)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    if (bus_live) { /* each case initializes the device anew: its mutex first goes */
        EMB_ASSERT_OK(emb_mutex_destroy(fake.bus.lock));
    }
    i2c_state = idle;
    (void)memset(&fake, 0, sizeof(fake));
    EMB_ASSERT_OK(emb_device_init(&i2c_dev));
    bus_live = true;
    emb_test_irq_connect(IRQ_DONE, fake_done_isr, NULL);
    emb_test_irq_connect(IRQ_REMOTE, fake_remote_isr, NULL);
}

EMB_TEST(i2c_write_then_read_a_register)
{
    static const uint8_t put[4] = {0x03u, 0xA1u, 0xB2u, 0xC3u}; /* register 3, three bytes */
    uint8_t reg = 0x04u;
    uint8_t got[2] = {0u};
    size_t done = 0u;
    const emb_drv_i2c_seg_t write[1] = {{put, NULL, sizeof(put)}};
    EMB_TEST_REQ("DRV-004", "DRV-005");
    ready();
    EMB_ASSERT_OK(emb_drv_i2c_transfer(&i2c_dev, &eeprom, write, 1u, EMB_WAIT_FOREVER, &done));
    EMB_ASSERT_EQ(done, sizeof(put));
    EMB_ASSERT_OK(emb_drv_i2c_write_read(&i2c_dev, &eeprom, &reg, 1u, got, sizeof(got),
                                         EMB_WAIT_FOREVER)); /* a repeated start between */
    EMB_ASSERT_TRUE(got[0] == 0xB2u && got[1] == 0xC3u);
    EMB_ASSERT_OK(emb_drv_i2c_write_read(&i2c_dev, &eeprom, NULL, 0u, got, 1u, EMB_WAIT_FOREVER));
    EMB_ASSERT_EQ(got[0], 0x00u); /* a read alone goes on from the pointer: register 6 */
}

EMB_TEST(i2c_segments_count_bytes)
{
    static const uint8_t a[2] = {0x00u, 0x11u};
    static const uint8_t b[2] = {0x22u, 0x33u};
    uint8_t back[3] = {0u};
    size_t done = 0u;
    const emb_drv_i2c_seg_t segs[2] = {{a, NULL, sizeof(a)}, {b, NULL, sizeof(b)}};
    EMB_TEST_REQ("DRV-005");
    ready();
    EMB_ASSERT_OK(emb_drv_i2c_transfer(&i2c_dev, &eeprom, segs, 2u, EMB_WAIT_FOREVER, &done));
    EMB_ASSERT_EQ(done, 4u);
    EMB_ASSERT_OK(
        emb_drv_i2c_write_read(&i2c_dev, &eeprom, a, 1u, back, sizeof(back), EMB_WAIT_FOREVER));
    EMB_ASSERT_TRUE(back[0] == 0x11u && back[1] == 0x22u && back[2] == 0x33u);
}

EMB_TEST(i2c_errors_and_timeout)
{
    uint8_t b = 0u;
    size_t done = 9u;
    const emb_drv_i2c_seg_t rd[1] = {{NULL, &b, 1u}};
    EMB_TEST_REQ("DRV-006");
    ready();
    EMB_ASSERT_STATUS(emb_drv_i2c_transfer(&i2c_dev, &absent, rd, 1u, EMB_WAIT_FOREVER, &done),
                      EMB_ENACK);
    EMB_ASSERT_EQ(done, 0u);
    EMB_ASSERT_STATUS(emb_drv_i2c_transfer(&i2c_dev, &too_fast, rd, 1u, EMB_WAIT_FOREVER, NULL),
                      EMB_ENOTSUP);
    fake.stall = 1u;
    EMB_ASSERT_STATUS(emb_drv_i2c_transfer(&i2c_dev, &eeprom, rd, 1u, EMB_TIMEOUT(EMB_MS(5)), NULL),
                      EMB_ETIMEDOUT);
    EMB_ASSERT_TRUE(fake.xfer == NULL); /* stopped */
}

EMB_TEST(i2c_target_answers_its_address)
{
    static const uint8_t reply[2] = {0x5Au, 0xA5u};
    emb_drv_i2c_req_t l;
    uint8_t rx[2] = {0u};
    EMB_TEST_REQ("DRV-004", "DRV-005");
    ready();
    fake.remote_addr = OWN;
    fake.remote_wr[0] = 0x10u;
    fake.remote_wr[1] = 0x20u;
    fake.remote_wr[2] = 0x30u; /* one more than the listen takes */
    fake.remote_wr_len = 3u;
    fake.remote_rd_len = 3u; /* one more than it has */
    emb_test_irq_raise(IRQ_REMOTE);
    EMB_ASSERT_EQ(fake.remote_acked, 0u); /* no listen yet: not acknowledged */
    emb_drv_i2c_req_listen(&l, OWN, rx, sizeof(rx), reply, sizeof(reply));
    EMB_ASSERT_OK(emb_drv_i2c_submit(&i2c_dev, &l));
    emb_test_irq_raise(IRQ_REMOTE);
    EMB_ASSERT_EQ(fake.remote_acked, 1u);
    EMB_ASSERT_STATUS(l.req.status, EMB_OK);
    EMB_ASSERT_EQ(l.req.done, 2u); /* received: the third was not acknowledged */
    EMB_ASSERT_TRUE(rx[0] == 0x10u && rx[1] == 0x20u);
    EMB_ASSERT_EQ(l.sent, 3u);
    EMB_ASSERT_TRUE(fake.remote_got[0] == 0x5Au && fake.remote_got[1] == 0xA5u &&
                    fake.remote_got[2] == 0xFFu); /* past the reply: ones */
}

EMB_TEST(i2c_controller_and_target_at_once)
{
    emb_drv_i2c_req_t l;
    uint8_t rx[1] = {0u};
    uint8_t reg = 0u;
    uint8_t got = 0u;
    size_t received = 9u;
    EMB_TEST_REQ("DRV-004");
    ready();
    emb_drv_i2c_req_listen(&l, OWN, rx, sizeof(rx), NULL, 0u);
    EMB_ASSERT_OK(emb_drv_i2c_submit(&i2c_dev, &l)); /* waits on the target lane */
    EMB_ASSERT_OK(emb_drv_i2c_write_read(&i2c_dev, &eeprom, &reg, 1u, &got, 1u, EMB_WAIT_FOREVER));
    EMB_ASSERT_TRUE(fake.listen == &l); /* still waiting, beside it */
    EMB_ASSERT_OK(emb_drv_cancel(&i2c_dev, &l.req));
    EMB_ASSERT_STATUS(l.req.status, EMB_ECANCELED);
    EMB_ASSERT_STATUS(emb_drv_i2c_listen(&i2c_dev, OWN, rx, sizeof(rx), NULL, 0u,
                                         EMB_TIMEOUT(EMB_MS(5)), &received, NULL),
                      EMB_ETIMEDOUT); /* nobody addressed it */
    EMB_ASSERT_EQ(received, 0u);
}

EMB_TEST(i2c_misuse_faults)
{
    static const emb_drv_i2c_target_t reserved = {EMB_DRV_I2C_STANDARD, 0x03u, {0u, 0u}};
    static const emb_drv_i2c_target_t no_clock = {0u, DEVICE, {0u, 0u}};
    uint8_t b = 0u;
    const emb_drv_i2c_seg_t ok[1] = {{NULL, &b, 1u}};
    const emb_drv_i2c_seg_t both[1] = {{&b, &b, 1u}};
    const emb_drv_i2c_seg_t neither[1] = {{NULL, NULL, 1u}};
    const emb_drv_i2c_seg_t empty[1] = {{NULL, &b, 0u}};
    emb_drv_i2c_req_t req;
    EMB_TEST_REQ("DRV-004");
    ready();
    emb_drv_i2c_req_transfer(&req, &eeprom, ok, 1u);
    EMB_ASSERT_FAULTS((void)emb_drv_i2c_submit(&plain_dev, &req)); /* not an I2C controller */
    EMB_ASSERT_FAULTS((void)emb_drv_i2c_submit(&i2c_dev, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_i2c_transfer(&i2c_dev, &reserved, ok, 1u, EMB_NO_WAIT, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_i2c_transfer(&i2c_dev, &no_clock, ok, 1u, EMB_NO_WAIT, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_i2c_transfer(&i2c_dev, &eeprom, both, 1u, EMB_NO_WAIT, NULL));
    EMB_ASSERT_FAULTS(
        (void)emb_drv_i2c_transfer(&i2c_dev, &eeprom, neither, 1u, EMB_NO_WAIT, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_i2c_transfer(&i2c_dev, &eeprom, empty, 1u, EMB_NO_WAIT, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_i2c_transfer(&i2c_dev, &eeprom, ok, 0u, EMB_NO_WAIT, NULL));
    EMB_ASSERT_FAULTS(
        (void)emb_drv_i2c_write_read(&i2c_dev, &eeprom, NULL, 0u, NULL, 0u, EMB_NO_WAIT));
    EMB_ASSERT_FAULTS(
        (void)emb_drv_i2c_write_read(&i2c_dev, &eeprom, &b, 0u, NULL, 0u, EMB_NO_WAIT));
    EMB_ASSERT_FAULTS(
        (void)emb_drv_i2c_listen(&i2c_dev, 0x7Cu, NULL, 0u, NULL, 0u, EMB_NO_WAIT, NULL, NULL));
    EMB_ASSERT_FAULTS(
        (void)emb_drv_i2c_listen(&i2c_dev, OWN, NULL, 4u, NULL, 0u, EMB_NO_WAIT, NULL, NULL));
}

EMB_TEST(i2c_held_bus_keeps_one_target)
{
    static const uint8_t put[2] = {0x08u, 0x5Au};
    const emb_drv_i2c_seg_t write[1] = {{put, NULL, sizeof(put)}};
    emb_drv_i2c_req_t req;
    uint8_t reg = 0x08u;
    uint8_t got = 0u;
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_OK(emb_drv_i2c_acquire(&i2c_dev, &eeprom, EMB_NO_WAIT));
    EMB_ASSERT_OK(emb_drv_i2c_transfer(&i2c_dev, &eeprom, write, 1u, EMB_WAIT_FOREVER, NULL));
    EMB_ASSERT_OK(emb_drv_i2c_write_read(&i2c_dev, &eeprom, &reg, 1u, &got, 1u, EMB_WAIT_FOREVER));
    EMB_ASSERT_EQ(got, 0x5Au); /* the holder's transactions run */
    EMB_ASSERT_STATUS(emb_drv_i2c_write_read(&i2c_dev, &absent, &reg, 1u, NULL, 0u, EMB_NO_WAIT),
                      EMB_EBUSY); /* another target's do not */
    emb_drv_i2c_req_transfer(&req, &absent, write, 1u);
    EMB_ASSERT_STATUS(emb_drv_i2c_submit(&i2c_dev, &req), EMB_EBUSY);
    EMB_ASSERT_OK(emb_drv_i2c_release(&i2c_dev));
    EMB_ASSERT_STATUS(emb_drv_i2c_release(&i2c_dev), EMB_EPERM); /* nothing held */
    EMB_ASSERT_STATUS(emb_drv_i2c_write_read(&i2c_dev, &absent, &reg, 1u, NULL, 0u, EMB_NO_WAIT),
                      EMB_ENACK); /* free again: it goes out, and nobody answers */
}
