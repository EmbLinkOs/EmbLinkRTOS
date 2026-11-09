/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The GPIO class (SPEC-017 §5a; DRV-004): its checks and a named line's sense, through a fake
 * controller of eight lines whose inputs read what its outputs drive. */
#include <emb_test.h>
#include <string.h>

#define FAKE_LINES 8u

typedef struct fake_gpio {
    uint32_t out;    /* the levels driven */
    uint32_t oe;     /* the outputs */
    uint32_t pulled; /* the inputs pulled up */
    uint32_t configured;
} fake_gpio_t;

static fake_gpio_t fake;
static emb_device_state_t gpio_state;

static emb_status_t fake_configure(const emb_device_t *dev, uint32_t line, uint32_t flags)
{
    uint32_t bit = 1u << line;
    (void)dev;
    if (line >= FAKE_LINES) {
        return EMB_EINVAL;
    }
    if ((flags & EMB_DRV_GPIO_OPEN_DRAIN) != 0u) {
        return EMB_ENOTSUP;
    }
    fake.configured++;
    if ((flags & EMB_DRV_GPIO_OUTPUT) != 0u) {
        fake.out = ((flags & EMB_DRV_GPIO_INIT_HIGH) != 0u) ? (fake.out | bit) : (fake.out & ~bit);
        fake.oe |= bit;
    } else {
        fake.oe &= ~bit;
    }
    fake.pulled = ((flags & EMB_DRV_GPIO_PULL_UP) != 0u) ? (fake.pulled | bit)
                                                         : (fake.pulled & ~bit);
    return EMB_OK;
}

static void fake_write(const emb_device_t *dev, uint32_t line, bool high)
{
    (void)dev;
    fake.out = high ? (fake.out | (1u << line)) : (fake.out & ~(1u << line));
}

static bool fake_read(const emb_device_t *dev, uint32_t line)
{
    uint32_t bit = 1u << line;
    (void)dev;
    if ((fake.oe & bit) != 0u) {
        return (fake.out & bit) != 0u; /* an output reads its own level */
    }
    return (fake.pulled & bit) != 0u; /* an input, nothing driving it: its pull */
}

static const emb_drv_gpio_api_t fake_api = {
    {NULL, NULL, NULL, NULL, NULL, 0u}, fake_configure, fake_write, fake_read};
static const emb_device_t gpio_dev = {
    "gpio", &fake_api.dev, NULL, &gpio_state, NULL, NULL, EMB_DRV_CLASS_GPIO, EMB_DEVICE_EARLY, 0u};
static const emb_device_api_t plain_api = {NULL, NULL, NULL, NULL, NULL, 0u};
static emb_device_state_t plain_state;
static const emb_device_t plain_dev = {
    "plain", &plain_api, NULL, &plain_state, NULL, NULL, EMB_DRV_CLASS_NONE, EMB_DEVICE_EARLY, 0u};

static void ready(void)
{
    (void)memset(&fake, 0, sizeof(fake));
}

EMB_TEST(gpio_output_drives_and_reads_back)
{
    bool high = false;
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_OK(
        emb_drv_gpio_configure(&gpio_dev, 3u, EMB_DRV_GPIO_OUTPUT | EMB_DRV_GPIO_INIT_HIGH));
    EMB_ASSERT_OK(emb_drv_gpio_read(&gpio_dev, 3u, &high));
    EMB_ASSERT_TRUE(high); /* the level it started with */
    EMB_ASSERT_OK(emb_drv_gpio_write(&gpio_dev, 3u, false));
    EMB_ASSERT_OK(emb_drv_gpio_read(&gpio_dev, 3u, &high));
    EMB_ASSERT_TRUE(!high);
    EMB_ASSERT_OK(emb_drv_gpio_write(&gpio_dev, 3u, true));
    EMB_ASSERT_OK(emb_drv_gpio_read(&gpio_dev, 3u, &high));
    EMB_ASSERT_TRUE(high);
}

EMB_TEST(gpio_input_reads_its_pull)
{
    bool high = false;
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_OK(emb_drv_gpio_configure(&gpio_dev, 5u, EMB_DRV_GPIO_INPUT | EMB_DRV_GPIO_PULL_UP));
    EMB_ASSERT_OK(emb_drv_gpio_read(&gpio_dev, 5u, &high));
    EMB_ASSERT_TRUE(high);
    EMB_ASSERT_OK(
        emb_drv_gpio_configure(&gpio_dev, 5u, EMB_DRV_GPIO_INPUT | EMB_DRV_GPIO_PULL_DOWN));
    EMB_ASSERT_OK(emb_drv_gpio_read(&gpio_dev, 5u, &high));
    EMB_ASSERT_TRUE(!high);
}

EMB_TEST(gpio_named_line_follows_its_sense)
{
    const emb_drv_gpio_line_t cs = {&gpio_dev, 2u, 1u, {0u, 0u}};
    const emb_drv_gpio_line_t reset = {&gpio_dev, 4u, 0u, {0u, 0u}};
    const emb_drv_gpio_line_t none = {NULL, 0u, 0u, {0u, 0u}};
    bool high = false;
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_OK(
        emb_drv_gpio_configure(&gpio_dev, 2u, EMB_DRV_GPIO_OUTPUT | EMB_DRV_GPIO_INIT_HIGH));
    EMB_ASSERT_OK(emb_drv_gpio_configure(&gpio_dev, 4u, EMB_DRV_GPIO_OUTPUT));
    EMB_ASSERT_OK(emb_drv_gpio_assert(&cs, true)); /* active low: asserted goes low */
    EMB_ASSERT_OK(emb_drv_gpio_read(&gpio_dev, 2u, &high));
    EMB_ASSERT_TRUE(!high);
    EMB_ASSERT_OK(emb_drv_gpio_assert(&cs, false));
    EMB_ASSERT_OK(emb_drv_gpio_read(&gpio_dev, 2u, &high));
    EMB_ASSERT_TRUE(high);
    EMB_ASSERT_OK(emb_drv_gpio_assert(&reset, true)); /* active high */
    EMB_ASSERT_OK(emb_drv_gpio_read(&gpio_dev, 4u, &high));
    EMB_ASSERT_TRUE(high);
    EMB_ASSERT_OK(emb_drv_gpio_assert(&none, true)); /* no line: nothing to do */
    EMB_ASSERT_OK(emb_drv_gpio_assert(NULL, true));
}

EMB_TEST(gpio_driver_refusals)
{
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_STATUS(emb_drv_gpio_configure(&gpio_dev, 9u, EMB_DRV_GPIO_OUTPUT), EMB_EINVAL);
    EMB_ASSERT_STATUS(
        emb_drv_gpio_configure(&gpio_dev, 1u, EMB_DRV_GPIO_OUTPUT | EMB_DRV_GPIO_OPEN_DRAIN),
        EMB_ENOTSUP);
    EMB_ASSERT_EQ(fake.configured, 0u); /* neither reached the line */
}

EMB_TEST(gpio_misuse_faults)
{
    bool high = false;
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_FAULTS((void)emb_drv_gpio_configure(&plain_dev, 0u, EMB_DRV_GPIO_OUTPUT));
    EMB_ASSERT_FAULTS((void)emb_drv_gpio_configure(NULL, 0u, EMB_DRV_GPIO_OUTPUT));
    EMB_ASSERT_FAULTS((void)emb_drv_gpio_configure(&gpio_dev, 32u, EMB_DRV_GPIO_OUTPUT));
    EMB_ASSERT_FAULTS(
        (void)emb_drv_gpio_configure(&gpio_dev, 0u, EMB_DRV_GPIO_PULL_UP | EMB_DRV_GPIO_PULL_DOWN));
    EMB_ASSERT_FAULTS(
        (void)emb_drv_gpio_configure(&gpio_dev, 0u, EMB_DRV_GPIO_INIT_HIGH)); /* an input */
    EMB_ASSERT_FAULTS((void)emb_drv_gpio_configure(&gpio_dev, 0u, 0x80u));
    EMB_ASSERT_FAULTS((void)emb_drv_gpio_write(&plain_dev, 0u, true));
    EMB_ASSERT_FAULTS((void)emb_drv_gpio_write(&gpio_dev, 32u, true));
    EMB_ASSERT_FAULTS((void)emb_drv_gpio_read(&gpio_dev, 0u, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_gpio_read(&plain_dev, 0u, &high));
    EMB_ASSERT_EQ(fake.configured, 0u);
}
