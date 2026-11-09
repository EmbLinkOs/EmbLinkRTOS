/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/*
 * Devices and driver requests (SPEC-016). The device's lock domain is a critical section
 * (§6): it covers the lifecycle, the queue and each queued request's phase, and is held only
 * for those, never across a driver's work or a completion target's delivery.
 */
#include <emb/device.h>
#include <emb/irq.h>
#include <emb/storage.h>

#include <embk/device.h>
#include <embk/kernel.h>

/* A request's phase: whose it is (emb_drv_request_t.phase). */
#define PHASE_IDLE     0u /* the caller's */
#define PHASE_QUEUED   1u /* in the device's queue, not started */
#define PHASE_STARTED  2u /* the head, given to the driver */
#define PHASE_STOPPING 3u /* the head, the driver asked to stop it */
#define PHASE_DONE     4u /* completed: the caller's again */

/* ---- the device ----------------------------------------------------------------- */

static bool deps_ready(const emb_device_t *dev)
{
    uint8_t i;
    /* bounded by the device's dependency count */
    for (i = 0u; i < dev->dep_count; i++) {
        if (dev->deps[i]->state->lifecycle != EMB_DEVICE_READY) {
            return false;
        }
    }
    return true;
}

static void set_lifecycle(const emb_device_t *dev, uint8_t lifecycle, emb_status_t why)
{
    emb_irq_key_t key = emb_irq_lock();
    dev->state->lifecycle = lifecycle;
    dev->state->last_status = (int16_t)why;
    emb_irq_unlock(key);
}

emb_status_t emb_device_init(const emb_device_t *dev)
{
    emb_status_t st;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_DEVICE_INIT);
    EMBK_API_REQUIRE(EMB_API_ID_DEVICE_INIT, dev != NULL && dev->state != NULL && dev->api != NULL,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    if (dev->state->lifecycle != EMB_DEVICE_UNINIT) {
        return EMB_EEXIST;
    }
    if (!deps_ready(dev)) {
        set_lifecycle(dev, EMB_DEVICE_FAILED, EMB_ENOENT); /* not its driver's failure */
        return EMB_ENOENT;
    }
    set_lifecycle(dev, EMB_DEVICE_INITIALIZING, EMB_OK);
    st = (dev->api->init != NULL) ? dev->api->init(dev) : EMB_OK;
    set_lifecycle(dev, (st == EMB_OK) ? EMB_DEVICE_READY : EMB_DEVICE_FAILED, st);
    return st;
}

emb_status_t emb_device_init_list(const emb_device_t *const *devs, size_t count)
{
    size_t i;
    bool failed = false;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_DEVICE_INIT_LIST);
    EMBK_API_REQUIRE(EMB_API_ID_DEVICE_INIT_LIST, devs != NULL || count == 0u,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    /* bounded by count; one failure does not stop the rest (DRV-003) */
    for (i = 0u; i < count; i++) {
        if (emb_device_init(devs[i]) != EMB_OK) {
            failed = true;
        }
    }
    return failed ? EMB_EIO : EMB_OK;
}

/* ---- the board's devices (SPEC-016 §4) ------------------------------------------ */

/* emb_board_device_table is the board's: the generated soc_devices.c defines it, or the
 * board support for a board without a description. It has no weak default, because a weak
 * definition here would satisfy the reference before the linker reached the board's
 * archive, and the board's devices would silently not be initialized. */

void embk_device_init_level(uint8_t level)
{
    const emb_device_table_t *t = &emb_board_device_table;
    size_t i = (level == EMB_DEVICE_EARLY) ? 0u : (size_t)t->early;
    size_t stop = (level == EMB_DEVICE_EARLY) ? (size_t)t->early : (size_t)t->count;
    /* bounded by the board's table */
    for (; i < stop; i++) {
        if (t->devices[i]->state->lifecycle == EMB_DEVICE_UNINIT) {
            /* a failure is the device's FAILED state and status; the rest go on (DRV-003) */
            (void)emb_device_init(t->devices[i]);
        }
    }
}

/* The accessors do not validate, as emb_irq_is_enabled() does not: a device is a generated
 * constant reached by symbol. */
uint8_t emb_device_state(const emb_device_t *dev)
{
    return dev->state->lifecycle;
}

bool emb_device_ready(const emb_device_t *dev)
{
    return dev->state->lifecycle == EMB_DEVICE_READY;
}

emb_status_t emb_device_status(const emb_device_t *dev)
{
    return (emb_status_t)dev->state->last_status;
}

emb_status_t emb_device_suspend(const emb_device_t *dev)
{
    emb_irq_key_t key;
    emb_status_t st;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_DEVICE_SUSPEND);
    EMBK_API_REQUIRE(EMB_API_ID_DEVICE_SUSPEND,
                     dev != NULL && dev->state != NULL && dev->api != NULL, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    key = emb_irq_lock();
    if (dev->state->lifecycle != EMB_DEVICE_READY) {
        emb_irq_unlock(key);
        return EMB_ESTATE;
    }
    if (dev->state->head[0] != NULL || dev->state->head[1] != NULL) {
        emb_irq_unlock(key);
        return EMB_EBUSY;
    }
    dev->state->lifecycle = EMB_DEVICE_SUSPENDED; /* no request can start from here on */
    emb_irq_unlock(key);
    st = (dev->api->suspend != NULL) ? dev->api->suspend(dev) : EMB_OK;
    if (st != EMB_OK) {
        set_lifecycle(dev, EMB_DEVICE_READY, EMB_OK); /* the driver kept it running */
    }
    return st;
}

emb_status_t emb_device_resume(const emb_device_t *dev)
{
    emb_status_t st;
    EMBK_API_REQUIRE_NOT_ISR(EMB_API_ID_DEVICE_RESUME);
    EMBK_API_REQUIRE(EMB_API_ID_DEVICE_RESUME,
                     dev != NULL && dev->state != NULL && dev->api != NULL, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    if (dev->state->lifecycle != EMB_DEVICE_SUSPENDED) {
        return EMB_ESTATE;
    }
    st = (dev->api->resume != NULL) ? dev->api->resume(dev) : EMB_OK;
    set_lifecycle(dev, (st == EMB_OK) ? EMB_DEVICE_READY : EMB_DEVICE_FAILED, st);
    return st;
}

/* ---- requests ------------------------------------------------------------------- */

void emb_drv_request_init(emb_drv_request_t *req)
{
    if (req == NULL) {
        return;
    }
    req->next = NULL;
    req->status = EMB_OK;
    req->done = 0u;
    req->target = EMB_DRV_TARGET_POLL;
    req->phase = PHASE_IDLE;
    req->lane = 0u;
    req->bits = 0u;
    req->thread.raw = 0u;
    req->sem.raw = 0u;
#if CONFIG_EMB_WORKQ
    req->workq.raw = 0u;
    req->work = NULL;
#endif
    req->callback = NULL;
    req->arg = NULL;
}

void emb_drv_on_notify(emb_drv_request_t *req, emb_thread_t thread, emb_notify_bits_t bits)
{
    if (req == NULL) {
        return;
    }
    req->target = EMB_DRV_TARGET_NOTIFY;
    req->thread = thread;
    req->bits = bits;
}

void emb_drv_on_sem(emb_drv_request_t *req, emb_sem_t sem)
{
    if (req == NULL) {
        return;
    }
    req->target = EMB_DRV_TARGET_SEM;
    req->sem = sem;
}

#if CONFIG_EMB_WORKQ
void emb_drv_on_work(emb_drv_request_t *req, emb_workq_t workq, emb_work_t *work)
{
    if (req == NULL || work == NULL) {
        return;
    }
    req->target = EMB_DRV_TARGET_WORK;
    req->workq = workq;
    req->work = work;
}
#endif

void emb_drv_on_callback(emb_drv_request_t *req, emb_drv_callback_t fn, void *arg)
{
    if (req == NULL || fn == NULL) {
        return;
    }
    req->target = EMB_DRV_TARGET_CALLBACK;
    req->callback = fn;
    req->arg = arg;
}

/* Delivers a completed request's target, outside the device's lock. */
static void deliver(emb_drv_request_t *req)
{
    switch (req->target) {
    case EMB_DRV_TARGET_NOTIFY:
        (void)emb_notify_set(req->thread, req->bits);
        break;
    case EMB_DRV_TARGET_SEM:
        (void)emb_sem_give(req->sem);
        break;
#if CONFIG_EMB_WORKQ
    case EMB_DRV_TARGET_WORK:
        (void)emb_work_submit(req->workq, req->work);
        break;
#endif
    case EMB_DRV_TARGET_CALLBACK:
        req->callback(req, req->arg);
        break;
    default: /* EMB_DRV_TARGET_POLL: the caller reads status */
        break;
    }
}

emb_status_t emb_drv_submit(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_irq_key_t key;
    bool start;
    uint8_t lane;
    uint8_t bit;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SUBMIT, dev != NULL && dev->state != NULL && dev->api != NULL,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SUBMIT, req != NULL && req->lane < EMB_DRV_LANES,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_SUBMIT, dev->api->start != NULL, EMB_FAULT_API_ARGUMENT,
                     EMB_EINVAL, 1u);
    lane = req->lane;
    bit = (uint8_t)(1u << lane);
    key = emb_irq_lock();
    if (dev->state->lifecycle != EMB_DEVICE_READY) {
        uint8_t lc = dev->state->lifecycle;
        emb_irq_unlock(key);
        return (lc == EMB_DEVICE_FAILED) ? EMB_EIO : EMB_ESTATE; /* DRV-003 */
    }
    if (req->phase == PHASE_QUEUED || req->phase == PHASE_STARTED || req->phase == PHASE_STOPPING) {
        emb_irq_unlock(key);
        return EMB_EBUSY;
    }
    req->next = NULL;
    req->status = EMB_EBUSY;
    req->done = 0u;
    req->phase = PHASE_QUEUED;
    if (dev->state->tail[lane] != NULL) {
        dev->state->tail[lane]->next = req;
    } else {
        dev->state->head[lane] = req;
    }
    dev->state->tail[lane] = req;
    /* whoever moves the lane from idle to busy starts the hardware (SPEC-016 §5) */
    start = ((dev->state->busy & bit) == 0u);
    if (start) {
        dev->state->busy = (uint8_t)(dev->state->busy | bit);
        req->phase = PHASE_STARTED;
    }
    emb_irq_unlock(key);
    if (start) {
        dev->api->start(dev, req);
    }
    return EMB_OK;
}

/* Removes req from its lane's queue under the lock; returns whether it was there. */
static bool queue_remove(emb_device_state_t *s, emb_drv_request_t *req)
{
    uint8_t lane = req->lane;
    emb_drv_request_t *prev = NULL;
    emb_drv_request_t *n = s->head[lane];
    /* bounded by the device's queued requests */
    while (n != NULL && n != req) {
        prev = n;
        n = n->next;
    }
    if (n == NULL) {
        return false;
    }
    if (prev != NULL) {
        prev->next = n->next;
    } else {
        s->head[lane] = n->next;
    }
    if (s->tail[lane] == n) {
        s->tail[lane] = prev;
    }
    n->next = NULL;
    return true;
}

void emb_drv_complete(const emb_device_t *dev, emb_drv_request_t *req, emb_status_t status,
                      uint32_t done)
{
    emb_irq_key_t key;
    emb_drv_request_t *next = NULL;
    if (dev == NULL || dev->state == NULL || dev->api == NULL || req == NULL) {
        return;
    }
    key = emb_irq_lock();
    if (req->phase != PHASE_STARTED && req->phase != PHASE_STOPPING) {
        emb_irq_unlock(key); /* completes once: a second completion does nothing */
        return;
    }
    (void)queue_remove(dev->state, req);
    req->status = status;
    req->done = done;
    req->phase = PHASE_DONE;
    next = dev->state->head[req->lane];
    if (next != NULL) {
        next->phase = PHASE_STARTED; /* the lane stays busy with it */
    } else {
        dev->state->busy = (uint8_t)(dev->state->busy & (uint8_t) ~(1u << req->lane));
    }
    emb_irq_unlock(key);
    deliver(req);
    if (next != NULL) {
        dev->api->start(dev, next);
    }
}

emb_status_t emb_drv_cancel(const emb_device_t *dev, emb_drv_request_t *req)
{
    emb_irq_key_t key;
    EMBK_API_REQUIRE(EMB_API_ID_DRV_CANCEL, dev != NULL && dev->state != NULL && dev->api != NULL,
                     EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 1u);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_CANCEL, req != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    key = emb_irq_lock();
    if (req->phase == PHASE_QUEUED) {
        (void)queue_remove(dev->state, req); /* not started: it ends here */
        req->status = EMB_ECANCELED;
        req->phase = PHASE_DONE;
        emb_irq_unlock(key);
        deliver(req);
        return EMB_OK;
    }
    if (req->phase == PHASE_STARTED) {
        req->phase = PHASE_STOPPING;
        emb_irq_unlock(key);
        if (dev->api->stop != NULL) {
            dev->api->stop(dev, req); /* the driver completes it with what it transferred */
        } else {
            emb_drv_complete(dev, req, EMB_ECANCELED, req->done);
        }
        return EMB_OK;
    }
    emb_irq_unlock(key);
    return EMB_ESTATE; /* already stopping, done, or never submitted */
}

emb_status_t emb_drv_wait(const emb_device_t *dev, emb_drv_request_t *req, emb_timeout_t timeout)
{
    emb_sem_storage_t storage; /* the wait's semaphore, in the caller's frame (SPEC-016 §5.2) */
    emb_sem_attr_t attr;
    emb_sem_t sem;
    emb_status_t st;
    EMBK_API_REQUIRE_THREAD(EMB_API_ID_DRV_WAIT);
    EMBK_API_REQUIRE(EMB_API_ID_DRV_WAIT, req != NULL, EMB_FAULT_API_ARGUMENT, EMB_EINVAL, 2u);
    emb_sem_attr_binary(&attr, false);
    st = emb_sem_init(&storage, &attr, &sem);
    if (st != EMB_OK) {
        return st;
    }
    emb_drv_on_sem(req, sem);
    st = emb_drv_submit(dev, req);
    if (st == EMB_OK) {
        if (emb_sem_take(sem, timeout) == EMB_ETIMEDOUT) {
            /* stop it and wait for its end, which the driver's stop bounds; a completion that
             * raced the timeout has already given the semaphore */
            (void)emb_drv_cancel(dev, req);
            (void)emb_sem_take(sem, EMB_WAIT_FOREVER);
            st = (req->status == EMB_ECANCELED) ? EMB_ETIMEDOUT : req->status;
        } else {
            st = req->status;
        }
    }
    (void)emb_sem_destroy(sem);
    return st;
}
