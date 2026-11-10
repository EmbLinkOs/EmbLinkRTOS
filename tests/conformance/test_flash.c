/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The flash class and partitions (SPEC-017 §9, A122; DRV-001, DRV-003, DRV-007): its checks
 * over a driver, through a NOR flash in RAM (flash_sim.c) of uneven sectors and a 4-byte write
 * unit, a power loss torn into a program and an erase, and the polled calls. */
#include "flash_sim.h"

#include <emb/flash_area.h>

#include <emb_test.h>
#include <string.h>

#define SIZE 4096u

static uint8_t mem[SIZE];
static const uint32_t sectors[4] = {512u, 512u, 1024u, 2048u};
static flash_sim_t sim;
static emb_device_state_t sim_state;
static const emb_device_t flash = {"flash",
                                   &flash_sim_api.dev,
                                   NULL,
                                   &sim_state,
                                   &sim,
                                   NULL,
                                   EMB_DRV_CLASS_FLASH,
                                   EMB_DEVICE_EARLY,
                                   0u};
static const emb_flash_area_t area = {"storage", &flash, 1024u, 1024u}; /* the third sector */

#define WAIT EMB_TIMEOUT(EMB_MS(100u))

/* The simulated flash as a new part: garbage, powered; the device initialized once, as its
 * lock is a kernel object. */
static void ready(void)
{
    emb_drv_flash_geometry_t g = {0u, SIZE, sectors, 0u, 4u, 4u, 0xFFu, {0u, 0u, 0u}};
    sim.geometry = g;
    flash_sim_reset(&sim, mem);
    if (!emb_device_ready(&flash)) {
        EMB_ASSERT_OK(emb_device_init(&flash));
    }
}

static bool all(const uint8_t *p, size_t n, uint8_t v)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        if (p[i] != v) {
            return false;
        }
    }
    return true;
}

EMB_TEST(flash_geometry_and_sectors)
{
    const emb_drv_flash_geometry_t *g = NULL;
    uint32_t start = 0u;
    uint32_t size = 0u;
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_flash_geometry(&flash, &g));
    EMB_ASSERT_EQ(g->size, SIZE);
    EMB_ASSERT_OK(emb_drv_flash_sector(&flash, 0u, &start, &size));
    EMB_ASSERT_TRUE((start == 0u) && (size == 512u));
    EMB_ASSERT_OK(emb_drv_flash_sector(&flash, 1023u, &start, &size));
    EMB_ASSERT_TRUE((start == 512u) && (size == 512u));
    EMB_ASSERT_OK(emb_drv_flash_sector(&flash, 2048u, &start, &size));
    EMB_ASSERT_TRUE((start == 2048u) && (size == 2048u));
    EMB_ASSERT_STATUS(emb_drv_flash_sector(&flash, SIZE, NULL, NULL), EMB_EINVAL); /* past it */
}

EMB_TEST(flash_program_read_erase)
{
    static const uint8_t data[8] = {1u, 2u, 3u, 4u, 0xF0u, 0x0Fu, 0xAAu, 0x55u};
    static const uint8_t ones[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t back[SIZE / 4u];
    EMB_TEST_REQ("DRV-001", "DRV-003");
    ready();
    EMB_ASSERT_OK(emb_drv_flash_erase(&flash, 0u, SIZE, WAIT)); /* every sector */
    EMB_ASSERT_OK(emb_drv_flash_read(&flash, 0u, back, sizeof(back), WAIT));
    EMB_ASSERT_TRUE(all(back, sizeof(back), 0xFFu));
    EMB_ASSERT_OK(emb_drv_flash_program(&flash, 1020u, data, sizeof(data), WAIT)); /* across */
    EMB_ASSERT_OK(emb_drv_flash_read(&flash, 1020u, back, sizeof(data), WAIT));
    EMB_ASSERT_TRUE(memcmp(back, data, sizeof(data)) == 0);
    /* NOR: bits programmed to 0 do not come back to 1 without an erase */
    EMB_ASSERT_STATUS(emb_drv_flash_program(&flash, 1020u, ones, sizeof(ones), WAIT), EMB_EVERIFY);
    EMB_ASSERT_OK(emb_drv_flash_erase(&flash, 512u, 512u, WAIT)); /* the second sector alone */
    EMB_ASSERT_OK(emb_drv_flash_read(&flash, 1020u, back, sizeof(data), WAIT));
    EMB_ASSERT_TRUE(all(back, 4u, 0xFFu) && (memcmp(&back[4], &data[4], 4u) == 0));
}

EMB_TEST(flash_refusals)
{
    static const uint8_t data[8] = {0u};
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    emb_device_state_t saved;
    uint8_t b = 0u;
    EMB_TEST_REQ("DRV-001", "DRV-003");
    ready();
    EMB_ASSERT_STATUS(emb_drv_flash_program(&flash, 2u, data, 4u, WAIT),
                      EMB_EINVAL); /* unaligned */
    EMB_ASSERT_STATUS(emb_drv_flash_program(&flash, 0u, data, 6u, WAIT), EMB_EINVAL); /* a part */
    EMB_ASSERT_STATUS(emb_drv_flash_program(&flash, SIZE - 4u, data, 8u, WAIT), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_drv_flash_erase(&flash, 256u, 256u, WAIT), EMB_EINVAL); /* mid-sector */
    EMB_ASSERT_STATUS(emb_drv_flash_erase(&flash, 0u, 768u, WAIT), EMB_EINVAL);   /* ends inside */
    EMB_ASSERT_STATUS(emb_drv_flash_read(&flash, SIZE, &b, 1u, WAIT), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_drv_flash_read(&flash, 0xFFFFFFFFu, &b, 2u, WAIT), EMB_EINVAL);
    EMB_ASSERT_OK(emb_drv_flash_read(&flash, SIZE, &b, 0u, WAIT)); /* nothing, at the end */
    saved = sim_state;
    sim_state = idle;
    EMB_ASSERT_STATUS(emb_drv_flash_read(&flash, 0u, &b, 1u, WAIT), EMB_ESTATE); /* not READY */
    sim_state = saved;
    EMB_ASSERT_FAULTS((void)emb_drv_flash_read(NULL, 0u, &b, 1u, WAIT));
    EMB_ASSERT_FAULTS((void)emb_drv_flash_read(&flash, 0u, NULL, 1u, WAIT));
    EMB_ASSERT_FAULTS((void)emb_drv_flash_program(&flash, 0u, NULL, 4u, WAIT));
    EMB_ASSERT_FAULTS((void)emb_flash_area_erase(NULL, 0u, 512u, WAIT));
}

EMB_TEST(flash_area_stays_inside)
{
    static const uint8_t data[4] = {9u, 8u, 7u, 6u};
    uint8_t back[4];
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_flash_erase(&flash, 0u, SIZE, WAIT));
    EMB_ASSERT_OK(emb_flash_area_program(&area, 1020u, data, sizeof(data), WAIT)); /* its end */
    EMB_ASSERT_OK(emb_drv_flash_read(&flash, 2044u, back, sizeof(back), WAIT));    /* the flash's */
    EMB_ASSERT_TRUE(memcmp(back, data, sizeof(data)) == 0);
    EMB_ASSERT_STATUS(emb_flash_area_program(&area, 1024u, data, sizeof(data), WAIT), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_flash_area_read(&area, 1022u, back, 4u, WAIT), EMB_EINVAL);
    EMB_ASSERT_STATUS(emb_flash_area_erase(&area, 0u, 2048u, WAIT), EMB_EINVAL); /* beyond it */
    EMB_ASSERT_OK(emb_flash_area_erase(&area, 0u, area.size, WAIT));             /* all of it */
    EMB_ASSERT_OK(emb_flash_area_read(&area, 1020u, back, sizeof(back), WAIT));
    EMB_ASSERT_TRUE(all(back, sizeof(back), 0xFFu));
}

EMB_TEST(flash_power_loss_tears_an_operation)
{
    static const uint8_t data[8] = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    uint8_t back[8];
    EMB_TEST_REQ("DRV-001");
    ready();
    EMB_ASSERT_OK(emb_drv_flash_erase(&flash, 0u, 1024u, WAIT));
    flash_sim_fail_at(&sim, 1u); /* the next program */
    EMB_ASSERT_STATUS(emb_drv_flash_program(&flash, 0u, data, sizeof(data), WAIT), EMB_EIO);
    EMB_ASSERT_STATUS(emb_drv_flash_erase(&flash, 0u, 512u, WAIT), EMB_EIO); /* no power */
    flash_sim_power_on(&sim);
    EMB_ASSERT_OK(emb_drv_flash_read(&flash, 0u, back, sizeof(back), WAIT));
    EMB_ASSERT_TRUE(all(back, 4u, 0x00u));     /* its first half programmed */
    EMB_ASSERT_EQ(back[4], 0x0Fu);             /* half a byte */
    EMB_ASSERT_TRUE(all(&back[5], 3u, 0xFFu)); /* the rest never reached */
    EMB_ASSERT_OK(emb_drv_flash_program(&flash, 256u, data, sizeof(data), WAIT));
    flash_sim_fail_at(&sim, 1u); /* now an erase */
    EMB_ASSERT_STATUS(emb_drv_flash_erase(&flash, 0u, 512u, WAIT), EMB_EIO);
    flash_sim_power_on(&sim);
    EMB_ASSERT_OK(emb_drv_flash_read(&flash, 0u, back, sizeof(back), WAIT));
    EMB_ASSERT_TRUE(all(back, sizeof(back), 0xFFu)); /* its first half erased */
    EMB_ASSERT_OK(emb_drv_flash_read(&flash, 256u, back, sizeof(back), WAIT));
    EMB_ASSERT_EQ(back[0], 0xF0u);                              /* half set where it stopped */
    EMB_ASSERT_TRUE(all(&back[1], 7u, 0x00u));                  /* the rest never reached */
    EMB_ASSERT_OK(emb_drv_flash_erase(&flash, 0u, 512u, WAIT)); /* and erased again, whole */
}

EMB_TEST(flash_polled_calls)
{
    static const uint8_t data[4] = {0x12u, 0x34u, 0x56u, 0x78u};
    uint8_t back[4];
    EMB_TEST_REQ("DRV-007");
    ready();
    EMB_ASSERT_OK(emb_drv_flash_poll_erase(&flash, 2048u, 2048u));
    EMB_ASSERT_OK(emb_drv_flash_poll_program(&flash, 4092u, data, sizeof(data)));
    EMB_ASSERT_OK(emb_drv_flash_poll_read(&flash, 4092u, back, sizeof(back)));
    EMB_ASSERT_TRUE(memcmp(back, data, sizeof(data)) == 0);
    EMB_ASSERT_STATUS(emb_drv_flash_poll_erase(&flash, 2048u, 1024u), EMB_EINVAL);
}
