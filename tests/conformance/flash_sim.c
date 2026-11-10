/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* A NOR flash in RAM (flash_sim.h). A torn program has programmed its first half, and of the
 * byte where it stopped only the high nibble; a torn erase has erased its first half and left
 * the byte where it stopped half set. */
#include "flash_sim.h"

static flash_sim_t *flash_sim_of(const emb_device_t *dev)
{
    return (flash_sim_t *)dev->data;
}

static emb_status_t flash_sim_init(const emb_device_t *dev)
{
    return emb_drv_flash_lock_init(dev);
}

static const emb_drv_flash_geometry_t *flash_sim_geometry(const emb_device_t *dev)
{
    return &flash_sim_of(dev)->geometry;
}

#define RUNS  0u /* powered: the operation runs */
#define TEARS 1u /* power is lost during it */
#define DEAD  2u /* power was lost before it */

/* Counts the operation, and says what becomes of it. */
static unsigned flash_sim_power(flash_sim_t *sim)
{
    if (sim->lost) {
        return DEAD;
    }
    sim->ops++;
    if ((sim->fail_at != 0u) && (sim->ops == sim->fail_at)) {
        sim->lost = true;
        return TEARS;
    }
    return RUNS;
}

static emb_status_t flash_sim_read(const emb_device_t *dev, uint32_t offset, void *buf, size_t len)
{
    const flash_sim_t *sim = flash_sim_of(dev);
    uint8_t *out = (uint8_t *)buf;
    size_t i;
    if (sim->lost) {
        return EMB_EIO;
    }
    for (i = 0u; i < len; i++) {
        out[i] = sim->mem[offset + i];
    }
    return EMB_OK;
}

static emb_status_t flash_sim_program(const emb_device_t *dev, uint32_t offset, const void *buf,
                                      size_t len)
{
    flash_sim_t *sim = flash_sim_of(dev);
    const uint8_t *in = (const uint8_t *)buf;
    size_t n = len;
    size_t i;
    unsigned power = flash_sim_power(sim);
    if (power == DEAD) {
        return EMB_EIO; /* nothing happens */
    }
    if (power == TEARS) {
        n = len / 2u; /* its first half, then a nibble */
        sim->mem[offset + n] &= (uint8_t)(in[n] | 0x0Fu);
    }
    for (i = 0u; i < n; i++) {
        sim->mem[offset + i] &= in[i]; /* NOR: bits only clear */
    }
    return (power == TEARS) ? EMB_EIO : EMB_OK;
}

static emb_status_t flash_sim_erase(const emb_device_t *dev, uint32_t offset, uint32_t len)
{
    flash_sim_t *sim = flash_sim_of(dev);
    uint32_t n = len;
    uint32_t i;
    unsigned power = flash_sim_power(sim);
    if (power == DEAD) {
        return EMB_EIO;
    }
    if (power == TEARS) {
        n = len / 2u; /* its first half, then a byte half set */
        sim->mem[offset + n] |= 0xF0u;
    }
    for (i = 0u; i < n; i++) {
        sim->mem[offset + i] = sim->geometry.erased;
    }
    return (power == TEARS) ? EMB_EIO : EMB_OK;
}

const emb_drv_flash_api_t flash_sim_api = {
    {flash_sim_init, NULL, NULL, NULL, NULL, 0u},
    flash_sim_geometry,
    flash_sim_read,
    flash_sim_program,
    flash_sim_erase,
};

void flash_sim_reset(flash_sim_t *sim, uint8_t *mem)
{
    uint32_t i;
    sim->mem = mem;
    for (i = 0u; i < sim->geometry.size; i++) {
        mem[i] = (uint8_t)(0x5Au ^ i); /* not erased: what a new part might hold */
    }
    sim->ops = 0u;
    sim->fail_at = 0u;
    sim->lost = false;
}

void flash_sim_fail_at(flash_sim_t *sim, unsigned n)
{
    sim->fail_at = (n == 0u) ? 0u : (sim->ops + n);
}

void flash_sim_power_on(flash_sim_t *sim)
{
    sim->lost = false;
    sim->fail_at = 0u;
}
