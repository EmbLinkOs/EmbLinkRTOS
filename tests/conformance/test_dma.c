/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The DMA class (SPEC-017 §7, A119; DRV-004, DRV-006): its checks over a driver's channels,
 * through a fake controller of two channels whose jobs end when the test says. A board whose
 * memory DMA reaches nothing (an emulator's without a DMA controller) skips what needs it. */
#include <stdint.h>

#include <emb_test.h>
#include <string.h>

typedef struct fake_chan {
    emb_drv_dma_done_t done;
    void *arg;
    emb_drv_dma_job_t job;
    uint8_t running;
} fake_chan_t;

static fake_chan_t chans[2];
static emb_device_state_t dma_state;
static emb_device_state_t plain_state;

static emb_status_t fake_start(const emb_device_t *dev, const emb_drv_dma_chan_t *ch,
                               const emb_drv_dma_job_t *job, emb_drv_dma_done_t done, void *arg)
{
    (void)dev;
    if (ch->channel >= 2u) {
        return EMB_EINVAL;
    }
    if (chans[ch->channel].running != 0u) {
        return EMB_EBUSY;
    }
    chans[ch->channel].done = done;
    chans[ch->channel].arg = arg;
    chans[ch->channel].job = *job;
    chans[ch->channel].running = 1u;
    return EMB_OK;
}

static emb_status_t fake_stop(const emb_device_t *dev, const emb_drv_dma_chan_t *ch,
                              uint32_t *moved)
{
    (void)dev;
    if ((ch->channel >= 2u) || (chans[ch->channel].running == 0u)) {
        return EMB_ESTATE;
    }
    chans[ch->channel].running = 0u;
    *moved = chans[ch->channel].job.count / 2u; /* half way */
    return EMB_OK;
}

/* The job on channel c ends, as its interrupt would say. */
static void fake_end(unsigned c, emb_status_t st)
{
    chans[c].running = 0u;
    chans[c].done(chans[c].arg, st, chans[c].job.count);
}

static const emb_drv_dma_api_t fake_api = {
    {NULL, NULL, NULL, NULL, NULL, EMB_DRV_DMA_CAP_MEM_TO_MEM}, fake_start, fake_stop};
static const emb_drv_dma_api_t plain_api = {
    {NULL, NULL, NULL, NULL, NULL, 0u}, fake_start, fake_stop}; /* no memory to memory */
static const emb_device_t dma_dev = {
    "dma", &fake_api.dev, NULL, &dma_state, NULL, NULL, EMB_DRV_CLASS_DMA, EMB_DEVICE_EARLY, 0u};
static const emb_device_t plain_dev = {
    "plain",           &plain_api.dev,   NULL, &plain_state, NULL, NULL,
    EMB_DRV_CLASS_DMA, EMB_DEVICE_EARLY, 0u};
static const emb_drv_dma_chan_t ch0 = {&dma_dev, 0u, 5u, {0u, 0u}};
static const emb_drv_dma_chan_t ch1 = {&dma_dev, 1u, 6u, {0u, 0u}};

static uint8_t src[16];
static uint8_t dst[16];
static emb_status_t ended_status;
static uint32_t ended_moved;
static unsigned ended;

static void on_end(void *arg, emb_status_t st, uint32_t moved)
{
    (void)arg;
    ended_status = st;
    ended_moved = moved;
    ended++;
}

static void ready(void)
{
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    dma_state = idle;
    plain_state = idle;
    (void)memset(chans, 0, sizeof(chans));
    ended = 0u;
    EMB_ASSERT_OK(emb_device_init(&dma_dev));
    EMB_ASSERT_OK(emb_device_init(&plain_dev));
}

static emb_drv_dma_job_t copy(uint32_t n)
{
    emb_drv_dma_job_t j = {(uintptr_t)src, (uintptr_t)dst, n, EMB_DRV_DMA_MEM_TO_MEM, 1u, 0u, 0u};
    return j;
}

/* The board's memory DMA reaches: the test's own buffers, or none at all. */
static bool reachable(void)
{
    emb_drv_dma_job_t j = copy(1u);
    if (emb_drv_dma_start(&ch1, &j, on_end, NULL) == EMB_EINVAL) {
        return false;
    }
    (void)emb_drv_dma_stop(&ch1, NULL);
    return true;
}

EMB_TEST(dma_job_starts_and_ends)
{
    emb_drv_dma_job_t j = copy(sizeof(src));
    EMB_TEST_REQ("DRV-004");
    ready();
    if (!reachable()) {
        emb_test_skip(EMB_TEST_STR("the board's memory is out of DMA's reach"));
        return;
    }
    EMB_ASSERT_OK(emb_drv_dma_start(&ch0, &j, on_end, NULL));
    EMB_ASSERT_TRUE(chans[0].job.count == sizeof(src) && chans[0].job.dst == (uintptr_t)dst);
    EMB_ASSERT_STATUS(emb_drv_dma_start(&ch0, &j, on_end, NULL), EMB_EBUSY); /* still running */
    EMB_ASSERT_OK(emb_drv_dma_start(&ch1, &j, on_end, NULL)); /* another channel runs beside it */
    fake_end(0u, EMB_OK);
    EMB_ASSERT_EQ(ended, 1u);
    EMB_ASSERT_STATUS(ended_status, EMB_OK);
    EMB_ASSERT_EQ(ended_moved, sizeof(src));
    fake_end(1u, EMB_EXFER);
    EMB_ASSERT_STATUS(ended_status, EMB_EXFER); /* a bus error, with what moved */
}

EMB_TEST(dma_stop_gives_what_moved)
{
    emb_drv_dma_job_t j = copy(sizeof(src));
    uint32_t moved = 99u;
    EMB_TEST_REQ("DRV-006");
    ready();
    if (!reachable()) {
        emb_test_skip(EMB_TEST_STR("the board's memory is out of DMA's reach"));
        return;
    }
    EMB_ASSERT_OK(emb_drv_dma_start(&ch0, &j, on_end, NULL));
    EMB_ASSERT_OK(emb_drv_dma_stop(&ch0, &moved));
    EMB_ASSERT_EQ(moved, sizeof(src) / 2u);
    EMB_ASSERT_EQ(ended, 0u);                                      /* no end after a stop */
    EMB_ASSERT_STATUS(emb_drv_dma_stop(&ch0, &moved), EMB_ESTATE); /* nothing running */
}

EMB_TEST(dma_refusals)
{
    emb_drv_dma_job_t j = copy(sizeof(src));
    static const emb_drv_dma_chan_t plain = {&plain_dev, 0u, 0u, {0u, 0u}};
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    EMB_TEST_REQ("DRV-004");
    ready();
    if (!reachable()) {
        EMB_ASSERT_STATUS(emb_drv_dma_start(&ch0, &j, on_end, NULL), EMB_EINVAL); /* no memory */
        return;
    }
    EMB_ASSERT_STATUS(emb_drv_dma_start(&plain, &j, on_end, NULL), EMB_ENOTSUP); /* no copy */
    if (emb_board_dma_regions[0].size != SIZE_MAX) {         /* a board's own memory map */
        static const uint8_t in_flash[4] = {1u, 2u, 3u, 4u}; /* where DMA must not read */
        j.src = (uintptr_t)in_flash;
        j.count = sizeof(in_flash);
        EMB_ASSERT_STATUS(emb_drv_dma_start(&ch0, &j, on_end, NULL), EMB_EINVAL);
    }
    j = copy(sizeof(src));
    dma_state = idle;
    EMB_ASSERT_STATUS(emb_drv_dma_start(&ch0, &j, on_end, NULL), EMB_ESTATE); /* not READY */
}

EMB_TEST(dma_misuse_faults)
{
    emb_drv_dma_job_t j = copy(1u);
    static const emb_drv_dma_chan_t nobody = {NULL, 0u, 0u, {0u, 0u}};
    EMB_TEST_REQ("DRV-004");
    ready();
    EMB_ASSERT_FAULTS((void)emb_drv_dma_start(NULL, &j, on_end, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_dma_start(&nobody, &j, on_end, NULL)); /* no channel given */
    EMB_ASSERT_FAULTS((void)emb_drv_dma_start(&ch0, NULL, on_end, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_dma_start(&ch0, &j, NULL, NULL));
    j.width = 3u;
    EMB_ASSERT_FAULTS((void)emb_drv_dma_start(&ch0, &j, on_end, NULL));
    j = copy(0u);
    EMB_ASSERT_FAULTS((void)emb_drv_dma_start(&ch0, &j, on_end, NULL));
    j = copy(1u);
    j.dir = 5u;
    EMB_ASSERT_FAULTS((void)emb_drv_dma_start(&ch0, &j, on_end, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_dma_stop(&nobody, NULL));
}
