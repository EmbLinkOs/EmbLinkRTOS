/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * This file is part of EmbLinkRTOS, a deterministic real-time kernel for microcontrollers.
 * Licensed under the Apache License, Version 2.0; see the LICENSE file at the repository root.
 */

/**
 * @file    test_device.c
 * @brief   Devices and driver requests (SPEC-016; DRV-001 to 006).
 *
 * @details
 * A fake driver whose requests end from a software interrupt, as a peripheral's would.
 *
 * @author  Junior Deogracias
 * @date    2026-11-09 (created)
 */
#include <emb_test.h>
#include <string.h>

#define IRQ_DONE 2u  /* software interrupt slot: the fake hardware's "transfer complete" */
#define PARTIAL  7u  /* what a stopped request reports as transferred */
#define FULL     32u /* what a finished one reports */

/* ---- the fake driver ------------------------------------------------------------ */

typedef struct fake_config {
    emb_status_t init_status; /* what init returns */
    bool sync;                /* complete inside start, as a polled driver would */
} fake_config_t;

typedef struct fake_data {
    unsigned inits;
    unsigned starts;
    unsigned stops;
    emb_drv_request_t *running; /* the request the "hardware" is working on */
    emb_drv_request_t *order[4];
} fake_data_t;

static emb_status_t fake_init(const emb_device_t *dev)
{
    const fake_config_t *cfg = (const fake_config_t *)dev->config;
    ((fake_data_t *)dev->data)->inits++;
    return cfg->init_status;
}

static emb_status_t fake_start(const emb_device_t *dev, emb_drv_request_t *req)
{
    fake_data_t *d = (fake_data_t *)dev->data;
    if (d->starts < 4u) {
        d->order[d->starts] = req;
    }
    d->starts++;
    d->running = req;
    if (((const fake_config_t *)dev->config)->sync) {
        d->running = NULL;
        req->done = FULL; /* finished inside start(): its status is the return (A115) */
        return EMB_OK;
    }
    return EMB_EBUSY;
}

static void fake_stop(const emb_device_t *dev, emb_drv_request_t *req)
{
    fake_data_t *d = (fake_data_t *)dev->data;
    d->stops++;
    d->running = NULL;
    emb_drv_complete(dev, req, EMB_ECANCELED, PARTIAL); /* stopped part-way */
}

static const emb_device_api_t fake_api = {fake_init, fake_start, fake_stop, NULL, NULL, 0u};

static const emb_device_t dev_async; /* the one device the interrupt serves */

/* The interrupt: the fake hardware finished the request it was given. */
EMB_TEST_ISR(fake_done_isr)
{
    const emb_device_t *dev = &dev_async;
    fake_data_t *d = (fake_data_t *)dev->data;
    (void)emb_isr_arg_;
    emb_drv_request_t *req = d->running;
    if (req != NULL) {
        d->running = NULL;
        emb_drv_complete(dev, req, EMB_OK, FULL);
    }
}

#define FAKE_DEVICE(sym, init_status_, sync_, deps_, dep_count_)                  \
    static const fake_config_t sym##_cfg = {(init_status_), (sync_)};             \
    static emb_device_state_t sym##_state;                                        \
    static fake_data_t sym##_data;                                                \
    static const emb_device_t sym = {#sym,         &fake_api,         &sym##_cfg, \
                                     &sym##_state, &sym##_data,       (deps_),    \
                                     0u,           EMB_DEVICE_KERNEL, (dep_count_)}

FAKE_DEVICE(dev_async, EMB_OK, false, NULL, 0u);
FAKE_DEVICE(dev_sync, EMB_OK, true, NULL, 0u);
FAKE_DEVICE(dev_bad, EMB_EIO, false, NULL, 0u);
static const emb_device_t *const on_bad[] = {&dev_bad};
FAKE_DEVICE(dev_dep, EMB_OK, false, on_bad, 1u);
FAKE_DEVICE(dev_after, EMB_OK, false, NULL, 0u);

/* A device back to UNINIT with no request, for a test of its own. */
static void reset(const emb_device_t *dev)
{
    fake_data_t zero = {0u, 0u, 0u, NULL, {NULL, NULL, NULL, NULL}};
    emb_device_state_t idle = {EMB_DEVICE_UNINIT, 0u, 0u, 0u, EMB_OK, {NULL, NULL}, {NULL, NULL}};
    *dev->state = idle;
    *(fake_data_t *)dev->data = zero;
}

static const emb_device_t *ready_async(void)
{
    reset(&dev_async);
    EMB_ASSERT_OK(emb_device_init(&dev_async));
    emb_test_irq_connect(IRQ_DONE, fake_done_isr, NULL);
    return &dev_async;
}

/* ---- lifecycle ------------------------------------------------------------------ */

EMB_TEST(device_init_reaches_ready_or_failed)
{
    EMB_TEST_REQ("DRV-002", "DRV-003");
    reset(&dev_sync);
    reset(&dev_bad);
    EMB_ASSERT_EQ(emb_device_state(&dev_sync), EMB_DEVICE_UNINIT);
    EMB_ASSERT_OK(emb_device_init(&dev_sync));
    EMB_ASSERT_EQ(emb_device_state(&dev_sync), EMB_DEVICE_READY);
    EMB_ASSERT_TRUE(emb_device_ready(&dev_sync));
    EMB_ASSERT_STATUS(emb_device_status(&dev_sync), EMB_OK);
    EMB_ASSERT_STATUS(emb_device_init(&dev_sync), EMB_EEXIST); /* once */

    EMB_ASSERT_STATUS(emb_device_init(&dev_bad), EMB_EIO);
    EMB_ASSERT_EQ(emb_device_state(&dev_bad), EMB_DEVICE_FAILED);
    EMB_ASSERT_TRUE(!emb_device_ready(&dev_bad));
    EMB_ASSERT_STATUS(emb_device_status(&dev_bad), EMB_EIO); /* the cause is kept */
}

EMB_TEST(device_on_a_failed_dependency_fails_without_its_driver)
{
    EMB_TEST_REQ("DRV-003");
    reset(&dev_bad);
    reset(&dev_dep);
    EMB_ASSERT_STATUS(emb_device_init(&dev_bad), EMB_EIO);
    EMB_ASSERT_STATUS(emb_device_init(&dev_dep), EMB_ENOENT);
    EMB_ASSERT_EQ(emb_device_state(&dev_dep), EMB_DEVICE_FAILED);
    EMB_ASSERT_STATUS(emb_device_status(&dev_dep), EMB_ENOENT);
    EMB_ASSERT_EQ(dev_dep_data.inits, 0u); /* its driver never ran */
}

EMB_TEST(device_list_initializes_past_a_failure)
{
    static const emb_device_t *const list[] = {&dev_bad, &dev_dep, &dev_after};
    EMB_TEST_REQ("DRV-001", "DRV-003");
    reset(&dev_bad);
    reset(&dev_dep);
    reset(&dev_after);
    EMB_ASSERT_STATUS(emb_device_init_list(list, 3u), EMB_EIO);
    EMB_ASSERT_EQ(emb_device_state(&dev_bad), EMB_DEVICE_FAILED);
    EMB_ASSERT_EQ(emb_device_state(&dev_dep), EMB_DEVICE_FAILED);  /* through its dependency */
    EMB_ASSERT_EQ(emb_device_state(&dev_after), EMB_DEVICE_READY); /* unrelated: unaffected */
}

EMB_TEST(device_suspend_and_resume)
{
    emb_drv_request_t req;
    const emb_device_t *dev = ready_async();
    EMB_TEST_REQ("DRV-002", "PWR-002");
    emb_drv_request_init(&req);
    EMB_ASSERT_OK(emb_drv_submit(dev, &req));
    EMB_ASSERT_STATUS(emb_device_suspend(dev), EMB_EBUSY); /* a request is running */
    emb_test_irq_raise(IRQ_DONE);
    EMB_ASSERT_OK(emb_device_suspend(dev));
    EMB_ASSERT_EQ(emb_device_state(dev), EMB_DEVICE_SUSPENDED);
    EMB_ASSERT_STATUS(emb_drv_submit(dev, &req), EMB_ESTATE);
    EMB_ASSERT_STATUS(emb_device_suspend(dev), EMB_ESTATE);
    EMB_ASSERT_OK(emb_device_resume(dev));
    EMB_ASSERT_EQ(emb_device_state(dev), EMB_DEVICE_READY);
    EMB_ASSERT_STATUS(emb_device_resume(dev), EMB_ESTATE);
}

/* ---- requests ------------------------------------------------------------------- */

EMB_TEST(drv_submit_refused_unless_ready)
{
    emb_drv_request_t req;
    EMB_TEST_REQ("DRV-003");
    reset(&dev_after);
    reset(&dev_bad);
    (void)emb_device_init(&dev_bad);
    emb_drv_request_init(&req);
    EMB_ASSERT_STATUS(emb_drv_submit(&dev_after, &req), EMB_ESTATE); /* UNINIT */
    EMB_ASSERT_STATUS(emb_drv_submit(&dev_bad, &req), EMB_EIO);      /* FAILED */
}

EMB_TEST(drv_request_completes_from_the_interrupt)
{
    emb_drv_request_t req;
    const emb_device_t *dev = ready_async();
    EMB_TEST_REQ("DRV-004", "DRV-005");
    emb_drv_request_init(&req); /* polled: the caller reads status */
    EMB_ASSERT_OK(emb_drv_submit(dev, &req));
    EMB_ASSERT_STATUS(req.status, EMB_EBUSY);
    EMB_ASSERT_STATUS(emb_drv_submit(dev, &req), EMB_EBUSY); /* already queued */
    emb_test_irq_raise(IRQ_DONE);
    EMB_ASSERT_OK(req.status);
    EMB_ASSERT_EQ(req.done, FULL);
}

static volatile unsigned callback_runs;
static void on_done(emb_drv_request_t *req, void *arg)
{
    (void)req;
    callback_runs += (unsigned)(uintptr_t)arg;
}

#if CONFIG_EMB_WORKQ
EMB_WORKQ_DEFINE(device_workq, 6u, EMB_TEST_STACK_SIZE);
static volatile unsigned work_runs;
static void work_fn(emb_work_t *work)
{
    (void)work;
    work_runs++;
}
#endif

EMB_TEST(drv_completion_targets)
{
    emb_drv_request_t req;
    emb_notify_bits_t got = 0u;
    emb_sem_storage_t storage;
    emb_sem_attr_t attr;
    emb_sem_t sem;
    const emb_device_t *dev = ready_async();
    EMB_TEST_REQ("DRV-005");

    emb_drv_request_init(&req);
    emb_drv_on_notify(&req, emb_thread_self(), EMB_NOTIFY_BIT(1));
    EMB_ASSERT_OK(emb_drv_submit(dev, &req));
    emb_test_irq_raise(IRQ_DONE);
    EMB_ASSERT_OK(
        emb_notify_wait(EMB_NOTIFY_BIT(1), EMB_NOTIFY_ANY | EMB_NOTIFY_CLEAR, EMB_NO_WAIT, &got));
    EMB_ASSERT_EQ(got, EMB_NOTIFY_BIT(1));

    emb_sem_attr_binary(&attr, false);
    EMB_ASSERT_OK(emb_sem_init(&storage, &attr, &sem));
    emb_drv_request_init(&req);
    emb_drv_on_sem(&req, sem);
    EMB_ASSERT_OK(emb_drv_submit(dev, &req));
    EMB_ASSERT_STATUS(emb_sem_take(sem, EMB_NO_WAIT), EMB_ETIMEDOUT);
    emb_test_irq_raise(IRQ_DONE);
    EMB_ASSERT_OK(emb_sem_take(sem, EMB_NO_WAIT));
    EMB_ASSERT_OK(emb_sem_destroy(sem));

    callback_runs = 0u;
    emb_drv_request_init(&req);
    emb_drv_on_callback(&req, on_done, (void *)3);
    EMB_ASSERT_OK(emb_drv_submit(dev, &req));
    emb_test_irq_raise(IRQ_DONE);
    EMB_ASSERT_EQ(callback_runs, 3u); /* once, in the interrupt */

#if CONFIG_EMB_WORKQ
    {
        emb_work_t work;
        work_runs = 0u;
        emb_work_init(&work, work_fn);
        emb_drv_request_init(&req);
        emb_drv_on_work(&req, device_workq, &work);
        EMB_ASSERT_OK(emb_drv_submit(dev, &req));
        emb_test_irq_raise(IRQ_DONE);
        EMB_ASSERT_EQ(work_runs, 1u); /* its queue's thread outranks this one */
    }
#endif
}

EMB_TEST(drv_queue_is_first_in_first_out)
{
    emb_drv_request_t a;
    emb_drv_request_t b;
    const emb_device_t *dev = ready_async();
    EMB_TEST_REQ("DRV-004");
    emb_drv_request_init(&a);
    emb_drv_request_init(&b);
    EMB_ASSERT_OK(emb_drv_submit(dev, &a));
    EMB_ASSERT_OK(emb_drv_submit(dev, &b));
    EMB_ASSERT_EQ(dev_async_data.starts, 1u); /* b waits for a */
    emb_test_irq_raise(IRQ_DONE);
    EMB_ASSERT_OK(a.status);
    EMB_ASSERT_EQ(dev_async_data.starts, 2u); /* a's completion started b */
    EMB_ASSERT_TRUE(dev_async_data.order[0] == &a && dev_async_data.order[1] == &b);
    emb_test_irq_raise(IRQ_DONE);
    EMB_ASSERT_OK(b.status);
}

EMB_TEST(drv_lanes_run_independently)
{
    const emb_device_t *dev = ready_async();
    emb_drv_request_t tx1;
    emb_drv_request_t tx2;
    emb_drv_request_t rx;
    EMB_TEST_REQ("DRV-004", "DRV-006");
    emb_drv_request_init(&tx1);
    emb_drv_request_init(&tx2);
    emb_drv_request_init(&rx);
    rx.lane = 1u; /* a full-duplex device: a read beside the writes (A114) */
    EMB_ASSERT_OK(emb_drv_submit(dev, &tx1));
    EMB_ASSERT_OK(emb_drv_submit(dev, &tx2)); /* behind tx1 on lane 0 */
    EMB_ASSERT_OK(emb_drv_submit(dev, &rx));  /* lane 1 is idle: it starts at once */
    EMB_ASSERT_EQ(dev_async_data.starts, 2u);
    EMB_ASSERT_TRUE(dev_async_data.order[1] == &rx);
    emb_drv_complete(dev, &rx, EMB_OK, 3u); /* the read ends first; lane 0 is untouched */
    EMB_ASSERT_STATUS(rx.status, EMB_OK);
    EMB_ASSERT_STATUS(tx2.status, EMB_EBUSY);
    EMB_ASSERT_EQ(dev_async_data.starts, 2u);
    EMB_ASSERT_STATUS(emb_device_suspend(dev), EMB_EBUSY); /* lane 0 still has requests */
    emb_drv_complete(dev, &tx1, EMB_OK, 1u);               /* tx2 starts after tx1 */
    EMB_ASSERT_EQ(dev_async_data.starts, 3u);
    EMB_ASSERT_TRUE(dev_async_data.order[2] == &tx2);
    emb_drv_complete(dev, &tx2, EMB_OK, 1u);
    EMB_ASSERT_OK(emb_device_suspend(dev)); /* both lanes empty */
    EMB_ASSERT_OK(emb_device_resume(dev));
    rx.lane = EMB_DRV_LANES; /* no such lane */
    EMB_ASSERT_FAULTS((void)emb_drv_submit(dev, &rx));
}

EMB_TEST(drv_cancel_queued_and_running_requests_complete_once)
{
    emb_drv_request_t a;
    emb_drv_request_t b;
    const emb_device_t *dev = ready_async();
    EMB_TEST_REQ("DRV-006");
    emb_drv_request_init(&a);
    emb_drv_request_init(&b);
    EMB_ASSERT_OK(emb_drv_submit(dev, &a));
    EMB_ASSERT_OK(emb_drv_submit(dev, &b));

    EMB_ASSERT_OK(emb_drv_cancel(dev, &b)); /* queued: ends at once, nothing transferred */
    EMB_ASSERT_STATUS(b.status, EMB_ECANCELED);
    EMB_ASSERT_EQ(b.done, 0u);
    EMB_ASSERT_EQ(dev_async_data.stops, 0u);

    EMB_ASSERT_OK(emb_drv_cancel(dev, &a)); /* running: the driver stops it */
    EMB_ASSERT_STATUS(a.status, EMB_ECANCELED);
    EMB_ASSERT_EQ(a.done, PARTIAL);
    EMB_ASSERT_EQ(dev_async_data.stops, 1u);

    emb_drv_complete(dev, &a, EMB_OK, FULL); /* a late completion changes nothing */
    EMB_ASSERT_STATUS(a.status, EMB_ECANCELED);
    EMB_ASSERT_STATUS(emb_drv_cancel(dev, &a), EMB_ESTATE);
    EMB_ASSERT_OK(emb_device_suspend(dev)); /* the queue is empty again */
    EMB_ASSERT_OK(emb_device_resume(dev));
}

EMB_TEST(drv_wait_returns_the_result_or_times_out)
{
    emb_drv_request_t req;
    EMB_TEST_REQ("DRV-004", "DRV-006");
    reset(&dev_sync);
    EMB_ASSERT_OK(emb_device_init(&dev_sync));
    emb_drv_request_init(&req);
    EMB_ASSERT_OK(emb_drv_wait(&dev_sync, &req, EMB_WAIT_FOREVER));
    EMB_ASSERT_EQ(req.done, FULL);

    (void)ready_async(); /* nothing raises its interrupt: the wait must time out */
    emb_drv_request_init(&req);
    EMB_ASSERT_STATUS(emb_drv_wait(&dev_async, &req, EMB_TIMEOUT(EMB_MS(10))), EMB_ETIMEDOUT);
    EMB_ASSERT_EQ(req.done, PARTIAL); /* what the stopped transfer moved */
    EMB_ASSERT_EQ(dev_async_data.stops, 1u);
    EMB_ASSERT_OK(emb_device_suspend(&dev_async)); /* nothing left queued */
}

/* ---- the board's devices (SPEC-016 §4) ------------------------------------------- */

EMB_TEST(device_board_devices_ready_in_dependency_order)
{
    const emb_device_table_t *t = &emb_board_device_table;
    uint16_t i;
    EMB_TEST_REQ("DRV-001", "DRV-002");
    EMB_ASSERT_TRUE(t->early <= t->count);
    /* bounded by the board's table */
    for (i = 0u; i < t->count; i++) {
        const emb_device_t *dev = t->devices[i];
        uint8_t d;
        /* the kernel initialized it: EARLY in emb_kernel_init(), KERNEL in emb_kernel_start() */
        EMB_ASSERT_EQ(emb_device_state(dev), EMB_DEVICE_READY);
        EMB_ASSERT_EQ(dev->level, (i < t->early) ? EMB_DEVICE_EARLY : EMB_DEVICE_KERNEL);
        /* bounded by its dependency count: each came before it in the table */
        for (d = 0u; d < dev->dep_count; d++) {
            uint16_t j = 0u;
            while (j < i && t->devices[j] != dev->deps[d]) { /* bounded by i */
                j++;
            }
            EMB_ASSERT_TRUE(j < i);
        }
    }
}

/* ---- optional operations and misuse ------------------------------------------------ */

static emb_status_t power_result; /* what the power operations below return */

static emb_status_t fake_power(const emb_device_t *dev)
{
    (void)dev;
    return power_result;
}

/* A driver with no init and no stop, and power operations; and one with nothing at all. */
static const emb_device_api_t bare_api = {NULL, fake_start, NULL, fake_power, fake_power, 0u};
static const emb_device_api_t empty_api = {NULL, NULL, NULL, NULL, NULL, 0u};

#define API_DEVICE(sym, api_)                               \
    static const fake_config_t sym##_cfg = {EMB_OK, false}; \
    static emb_device_state_t sym##_state;                  \
    static fake_data_t sym##_data;                          \
    static const emb_device_t sym = {                       \
        #sym, (api_), &sym##_cfg, &sym##_state, &sym##_data, NULL, 0u, EMB_DEVICE_KERNEL, 0u}

API_DEVICE(dev_bare, &bare_api);
API_DEVICE(dev_empty, &empty_api);

EMB_TEST(device_optional_operations)
{
    emb_drv_request_t a;
    emb_drv_request_t b;
    EMB_TEST_REQ("DRV-002", "DRV-006", "PWR-002");
    reset(&dev_bare);
    EMB_ASSERT_OK(emb_device_init(&dev_bare)); /* no init operation: READY at once */
    EMB_ASSERT_TRUE(emb_device_ready(&dev_bare));

    power_result = EMB_EIO; /* the driver refuses to suspend: the device stays READY */
    EMB_ASSERT_STATUS(emb_device_suspend(&dev_bare), EMB_EIO);
    EMB_ASSERT_EQ(emb_device_state(&dev_bare), EMB_DEVICE_READY);
    power_result = EMB_OK;
    EMB_ASSERT_OK(emb_device_suspend(&dev_bare));
    power_result = EMB_EIO; /* it cannot come back: FAILED, with the driver's status */
    EMB_ASSERT_STATUS(emb_device_resume(&dev_bare), EMB_EIO);
    EMB_ASSERT_EQ(emb_device_state(&dev_bare), EMB_DEVICE_FAILED);
    EMB_ASSERT_STATUS(emb_device_status(&dev_bare), EMB_EIO);
    power_result = EMB_OK;

    reset(&dev_bare);
    EMB_ASSERT_OK(emb_device_init(&dev_bare));
    emb_drv_request_init(&a);
    emb_drv_request_init(&b);
    EMB_ASSERT_OK(emb_drv_submit(&dev_bare, &a));                /* started, never finishes */
    EMB_ASSERT_STATUS(emb_drv_submit(&dev_bare, &a), EMB_EBUSY); /* not twice */
    EMB_ASSERT_OK(emb_drv_submit(&dev_bare, &b));
    EMB_ASSERT_STATUS(emb_drv_submit(&dev_bare, &b), EMB_EBUSY); /* queued: not twice either */
    EMB_ASSERT_OK(emb_drv_cancel(&dev_bare, &a)); /* no stop operation: the core completes it */
    EMB_ASSERT_STATUS(a.status, EMB_ECANCELED);
    EMB_ASSERT_EQ(a.done, 0u);
    EMB_ASSERT_OK(emb_drv_cancel(&dev_bare, &b)); /* b had started when a ended */
    EMB_ASSERT_STATUS(b.status, EMB_ECANCELED);

    reset(&dev_bare);
    emb_drv_request_init(&a);
    EMB_ASSERT_STATUS(emb_drv_wait(&dev_bare, &a, EMB_NO_WAIT), EMB_ESTATE); /* not READY */

    reset(&dev_empty);
    EMB_ASSERT_OK(emb_device_init(&dev_empty));
    EMB_ASSERT_OK(emb_device_suspend(&dev_empty)); /* no power operations: nothing to do */
    EMB_ASSERT_OK(emb_device_resume(&dev_empty));
    EMB_ASSERT_TRUE(emb_device_ready(&dev_empty));
}

EMB_TEST(drv_setters_ignore_a_missing_request)
{
    emb_drv_request_t req;
    emb_sem_t no_sem;
    EMB_TEST_REQ("DRV-005");
    no_sem.raw = 0u;
    emb_drv_request_init(NULL); /* each returns without touching anything */
    emb_drv_on_notify(NULL, emb_thread_self(), EMB_NOTIFY_BIT(1));
    emb_drv_on_sem(NULL, no_sem);
    emb_drv_on_callback(NULL, NULL, NULL);
    emb_drv_request_init(&req);
    emb_drv_on_callback(&req, NULL, NULL); /* no function: the target stays polled */
    EMB_ASSERT_EQ(req.target, EMB_DRV_TARGET_POLL);
#if CONFIG_EMB_WORKQ
    emb_drv_on_work(NULL, device_workq, NULL);
    emb_drv_on_work(&req, device_workq, NULL);
    EMB_ASSERT_EQ(req.target, EMB_DRV_TARGET_POLL);
#endif
    emb_drv_complete(NULL, &req, EMB_OK, 0u); /* a completion without its device or request */
    emb_drv_complete(&dev_sync, NULL, EMB_OK, 0u);
    EMB_ASSERT_EQ(req.phase, 0u);
}

EMB_TEST(device_misuse_faults)
{
    emb_drv_request_t req;
    EMB_TEST_REQ("DRV-002", "DRV-004");
    emb_drv_request_init(&req);
    reset(&dev_empty);
    EMB_ASSERT_OK(emb_device_init(&dev_empty));
    EMB_ASSERT_FAULTS((void)emb_device_init(NULL));
    EMB_ASSERT_FAULTS((void)emb_device_init_list(NULL, 1u));
    EMB_ASSERT_FAULTS((void)emb_device_suspend(NULL));
    EMB_ASSERT_FAULTS((void)emb_device_resume(NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_submit(NULL, &req));
    EMB_ASSERT_FAULTS((void)emb_drv_submit(&dev_sync, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_submit(&dev_empty, &req)); /* a driver with no start */
    EMB_ASSERT_FAULTS((void)emb_drv_cancel(NULL, &req));
    EMB_ASSERT_FAULTS((void)emb_drv_cancel(&dev_sync, NULL));
    EMB_ASSERT_FAULTS((void)emb_drv_wait(&dev_sync, NULL, EMB_NO_WAIT));
}

/* ---- the classes' status codes (SPEC-017 §3) ---------------------------------------- */

EMB_TEST(drv_status_codes_are_named_in_their_ranges)
{
    static const struct {
        emb_status_t code;
        const char *name;
    } codes[] = {
        {EMB_EFRAMING, "EMB_EFRAMING"},     {EMB_EPARITY, "EMB_EPARITY"},
        {EMB_EOVERRUN, "EMB_EOVERRUN"},     {EMB_ECRC, "EMB_ECRC"},
        {EMB_EMODEFAULT, "EMB_EMODEFAULT"}, {EMB_ENACK, "EMB_ENACK"},
        {EMB_EARBLOST, "EMB_EARBLOST"},     {EMB_EBUSERR, "EMB_EBUSERR"},
        {EMB_EXFER, "EMB_EXFER"},           {EMB_EWINDOW, "EMB_EWINDOW"},
        {EMB_EWRPROT, "EMB_EWRPROT"},       {EMB_EVERIFY, "EMB_EVERIFY"},
        {EMB_ENOTSET, "EMB_ENOTSET"},       {EMB_EOSCFAIL, "EMB_EOSCFAIL"},
        {EMB_EHEALTH, "EMB_EHEALTH"},       {EMB_EPAST, "EMB_EPAST"},
        {EMB_ENOSIGNAL, "EMB_ENOSIGNAL"},
    };
    unsigned i;
    EMB_TEST_REQ("DRV-008");
    for (i = 0u; i < sizeof(codes) / sizeof(codes[0]); i++) { /* bounded by the table */
        /* in the driver range, each class's four codes apart from the others' */
        EMB_ASSERT_TRUE(codes[i].code <= -64 && codes[i].code >= -107);
        EMB_ASSERT_TRUE(i == 0u || codes[i].code < codes[i - 1u].code);
#if CONFIG_EMB_STATUS_NAMES
        EMB_ASSERT_TRUE(strcmp(emb_status_name(codes[i].code), codes[i].name) == 0);
#endif
    }
#if CONFIG_EMB_STATUS_NAMES
    EMB_ASSERT_TRUE(strcmp(emb_status_name(-67), "EMB_E?") == 0);  /* the UART's spare code */
    EMB_ASSERT_TRUE(strcmp(emb_status_name(-104), "EMB_E?") == 0); /* the ADC's, none yet */
    EMB_ASSERT_TRUE(strcmp(emb_status_name(-108), "EMB_E?") == 0); /* past M4's classes */
#endif
}
