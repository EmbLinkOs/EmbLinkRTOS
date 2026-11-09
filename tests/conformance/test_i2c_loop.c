/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The I2C class on real controllers (SPEC-017 §6; DRV-004 to DRV-006): two of the chip's own
 * joined SCL to SCL and SDA to SDA, one the controller, the other a target answering its own
 * address, on the bench's wires or EmbSIM's. Built where they exist: EMB_TEST_I2C_LOOP names
 * the two devices (tests/conformance/CMakeLists.txt). The lines' pull-ups are the chip's own,
 * which the pin table turns on: 100 kHz and 400 kHz on short wires. */
#include <emb_test.h>
#include <soc_devices.h>
#include <string.h>

#define CTRL EMB_TEST_I2C_CTRL_DEV
#define TGT  EMB_TEST_I2C_TGT_DEV
#define OWN  0x42u

static const emb_drv_i2c_target_t peer = {EMB_DRV_I2C_STANDARD, OWN, {0u, 0u}};
static const emb_drv_i2c_target_t peer_fast = {EMB_DRV_I2C_FAST, OWN, {0u, 0u}};
static const emb_drv_i2c_target_t nobody = {EMB_DRV_I2C_STANDARD, 0x43u, {0u, 0u}};

static emb_drv_i2c_req_t l;
static uint8_t rx[16];

static void listen(const uint8_t *tx, size_t tx_len, size_t rx_len)
{
    (void)memset(rx, 0, sizeof(rx));
    emb_drv_i2c_req_listen(&l, OWN, rx, rx_len, tx, tx_len);
    EMB_ASSERT_OK(emb_drv_i2c_submit(TGT, &l));
}

/* The target's stop arrives a moment after the controller's own. */
static void settle(void)
{
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(5)));
    if (l.req.status == EMB_EBUSY) {
        (void)emb_drv_cancel(TGT, &l.req); /* a case that failed: the next starts clean */
    }
}

EMB_TEST(i2c_loop_write_to_the_target)
{
    static const uint8_t out[6] = {0x00u, 0x5Au, 0xA5u, 0xFFu, 0x01u, 0x80u};
    const emb_drv_i2c_seg_t w[1] = {{out, NULL, sizeof(out)}};
    size_t done = 0u;
    EMB_TEST_REQ("DRV-004", "DRV-005");
    EMB_ASSERT_TRUE(emb_device_ready(CTRL) && emb_device_ready(TGT));
    listen(NULL, 0u, sizeof(rx));
    EMB_ASSERT_OK(emb_drv_i2c_transfer(CTRL, &peer, w, 1u, EMB_TIMEOUT(EMB_MS(100)), &done));
    EMB_ASSERT_EQ(done, sizeof(out));
    settle();
    EMB_ASSERT_STATUS(l.req.status, EMB_OK);
    EMB_ASSERT_EQ(l.req.done, sizeof(out));
    EMB_ASSERT_TRUE(memcmp(rx, out, sizeof(out)) == 0);
}

EMB_TEST(i2c_loop_register_reads_of_every_length)
{
    static const uint8_t regs[8] = {0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u};
    static const size_t lens[4] = {1u, 2u, 3u, 8u}; /* the STM32's three receive sequences */
    size_t i;
    EMB_TEST_REQ("DRV-004", "DRV-005");
    for (i = 0u; i < 4u; i++) { /* bounded: four lengths */
        uint8_t reg = 0x10u;
        uint8_t got[8] = {0u};
        listen(regs, sizeof(regs), sizeof(rx));
        EMB_ASSERT_OK(
            emb_drv_i2c_write_read(CTRL, &peer, &reg, 1u, got, lens[i], EMB_TIMEOUT(EMB_MS(100))));
        EMB_ASSERT_TRUE(memcmp(got, regs, lens[i]) == 0);
        settle();
        EMB_ASSERT_STATUS(l.req.status, EMB_OK);
        EMB_ASSERT_EQ(l.req.done, 1u); /* the register's address */
        EMB_ASSERT_EQ(rx[0], 0x10u);
        EMB_ASSERT_EQ(l.sent, lens[i]);
    }
}

EMB_TEST(i2c_loop_nobody_answers)
{
    uint8_t b = 0u;
    const emb_drv_i2c_seg_t r[1] = {{NULL, &b, 1u}};
    size_t done = 9u;
    EMB_TEST_REQ("DRV-006");
    EMB_ASSERT_STATUS(emb_drv_i2c_transfer(CTRL, &nobody, r, 1u, EMB_TIMEOUT(EMB_MS(100)), &done),
                      EMB_ENACK);
    EMB_ASSERT_EQ(done, 0u);
    listen(NULL, 0u, sizeof(rx)); /* the bus is free again: the next transaction goes through */
    EMB_ASSERT_OK(emb_drv_i2c_write_read(CTRL, &peer, &b, 1u, NULL, 0u, EMB_TIMEOUT(EMB_MS(100))));
    settle();
    EMB_ASSERT_STATUS(l.req.status, EMB_OK);
}

/* Bytes beyond a listen's buffer: refused by a target that can (TARGET_NACK), acknowledged and
 * dropped by one that cannot; either way the target keeps what fits. */
EMB_TEST(i2c_loop_target_drops_beyond_its_buffer)
{
    static const uint8_t out[4] = {1u, 2u, 3u, 4u};
    const emb_drv_i2c_seg_t w[1] = {{out, NULL, sizeof(out)}};
    size_t done = 9u;
    emb_status_t st;
    EMB_TEST_REQ("DRV-006");
    listen(NULL, 0u, 2u);
    st = emb_drv_i2c_transfer(CTRL, &peer, w, 1u, EMB_TIMEOUT(EMB_MS(100)), &done);
    if ((emb_device_caps(TGT) & EMB_DRV_I2C_CAP_TARGET_NACK) != 0u) {
        EMB_ASSERT_STATUS(st, EMB_ENACK); /* the third byte is refused */
        EMB_ASSERT_EQ(done, 2u);
    } else {
        EMB_ASSERT_OK(st);
        EMB_ASSERT_EQ(done, sizeof(out));
    }
    settle();
    EMB_ASSERT_EQ(l.req.done, 2u);
    EMB_ASSERT_TRUE(rx[0] == 1u && rx[1] == 2u);
}

EMB_TEST(i2c_loop_fast_mode)
{
    static const uint8_t regs[4] = {0xDEu, 0xADu, 0xBEu, 0xEFu};
    uint8_t reg = 0x00u;
    uint8_t got[4] = {0u};
    EMB_TEST_REQ("DRV-004");
    listen(regs, sizeof(regs), sizeof(rx));
    EMB_ASSERT_OK(emb_drv_i2c_write_read(CTRL, &peer_fast, &reg, 1u, got, sizeof(got),
                                         EMB_TIMEOUT(EMB_MS(100))));
    EMB_ASSERT_TRUE(memcmp(got, regs, sizeof(got)) == 0);
    settle();
    EMB_ASSERT_STATUS(l.req.status, EMB_OK);
}

EMB_TEST(i2c_loop_listen_times_out)
{
    size_t received = 9u;
    EMB_TEST_REQ("DRV-006");
    EMB_ASSERT_STATUS(emb_drv_i2c_listen(TGT, OWN, rx, sizeof(rx), NULL, 0u,
                                         EMB_TIMEOUT(EMB_MS(10)), &received, NULL),
                      EMB_ETIMEDOUT); /* no controller addressed it */
    EMB_ASSERT_EQ(received, 0u);
}

#ifdef EMB_TEST_I2C_CTRL_SCL
/* A target cut off in the middle of a byte it sends: the test, as a broken controller, clocks
 * by hand a start, the target's address to read, its acknowledge and two bits of a byte of
 * zeros, and stops. The target holds SDA low for its third bit; recovery clocks it free. */
static const emb_drv_gpio_line_t scl = EMB_TEST_I2C_CTRL_SCL;
static const emb_drv_gpio_line_t sda = EMB_TEST_I2C_CTRL_SDA;

static void half(void)
{
    EMB_ASSERT_OK(emb_thread_sleep(EMB_MS(1))); /* I2C has no slowest clock */
}

static void put(const emb_drv_gpio_line_t *w, bool high)
{
    EMB_ASSERT_OK(emb_drv_gpio_write(w->gpio, w->line, high));
}

static bool level(const emb_drv_gpio_line_t *w)
{
    bool high = false;
    EMB_ASSERT_OK(emb_drv_gpio_read(w->gpio, w->line, &high));
    return high;
}

static void clock_bit(bool bit)
{
    put(&sda, bit);
    half();
    put(&scl, true);
    half();
    put(&scl, false);
    half();
}

EMB_TEST(i2c_loop_recovers_a_stuck_target)
{
    static const uint8_t zeros[8] = {0u};
    const uint32_t od = EMB_DRV_GPIO_OUTPUT | EMB_DRV_GPIO_OPEN_DRAIN | EMB_DRV_GPIO_PULL_UP |
                        EMB_DRV_GPIO_INIT_HIGH;
    uint32_t addr = (OWN << 1) | 1u;
    uint8_t reg = 0x00u;
    uint8_t got[2] = {0u};
    int bit;
    EMB_TEST_REQ("DRV-006");
    listen(zeros, sizeof(zeros), sizeof(rx));
    EMB_ASSERT_OK(emb_drv_gpio_configure(sda.gpio, sda.line, od));
    EMB_ASSERT_OK(emb_drv_gpio_configure(scl.gpio, scl.line, od));
    half();
    put(&sda, false); /* start: SDA falls while SCL is high */
    half();
    put(&scl, false);
    half();
    for (bit = 7; bit >= 0; bit--) { /* the address, to read */
        clock_bit(((addr >> (uint32_t)bit) & 1u) != 0u);
    }
    clock_bit(true); /* released for the target's acknowledge */
    clock_bit(true); /* two bits of its byte, both zeros, and no more */
    clock_bit(true);
    EMB_ASSERT_TRUE(!level(&sda)); /* the third bit: the target holds SDA low */
    EMB_ASSERT_OK(emb_drv_gpio_configure(scl.gpio, scl.line, EMB_DRV_GPIO_FUNCTION));
    EMB_ASSERT_OK(emb_drv_gpio_configure(sda.gpio, sda.line, EMB_DRV_GPIO_FUNCTION));
    EMB_ASSERT_OK(emb_drv_i2c_recover(CTRL, EMB_TIMEOUT(EMB_MS(100))));
    EMB_ASSERT_TRUE(level(&sda) && level(&scl)); /* free */
    settle();                                    /* the target's read ended: refused, then a stop */
    EMB_ASSERT_STATUS(l.req.status, EMB_OK);
    listen(zeros, sizeof(zeros), sizeof(rx));
    EMB_ASSERT_OK(emb_drv_i2c_write_read(CTRL, &peer, &reg, 1u, got, sizeof(got),
                                         EMB_TIMEOUT(EMB_MS(100)))); /* and the bus works */
    settle();
    EMB_ASSERT_STATUS(l.req.status, EMB_OK);
}
#endif
