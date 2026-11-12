/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* Status codes (SPEC-001 §4). Values are stable forever once released (API-010). */
#ifndef EMB_STATUS_H
#define EMB_STATUS_H

#include <emb/config.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int emb_status_t;

enum emb_status_code {
    EMB_OK = 0,
    EMB_EPERM = -1,      /* wrong context, not the owner, insufficient rights */
    EMB_EINVAL = -2,     /* invalid argument or handle */
    EMB_EBUSY = -3,      /* in use; cannot proceed without a wait that is not allowed */
    EMB_ETIMEDOUT = -4,  /* the wait ended without satisfaction, including EMB_NO_WAIT */
    EMB_ECANCELED = -5,  /* the calling thread was canceled */
    EMB_EDESTROYED = -6, /* the object was destroyed with ABORT_WAITERS */
    EMB_EOWNERDEAD = -7, /* the previous mutex owner terminated while holding it */
    EMB_ENOMEM = -8,     /* capacity exhausted */
    EMB_ENOTSUP = -9,    /* not supported by this configuration, target, or device */
    EMB_EIO = -10,       /* hardware or transport error */
    EMB_EFAULT = -11,    /* address outside the caller's accessible regions */
    EMB_ENOENT = -12,    /* no such object, device, or entry */
    EMB_EEXIST = -13,    /* already exists, initialized, or started */
    EMB_ESTATE = -14,    /* the object or thread state does not permit the operation */
    EMB_EOVERFLOW = -15, /* value or size exceeds what can be represented or stored */
    EMB_ESTALE = -16,    /* handle refers to an object generation that no longer exists */
    EMB_EINTR = -17,     /* reserved; not returned in 1.0 */
    EMB_EDEADLK =
        -32, /* the lock would complete a cycle; first kernel-reserved code (SPEC-005 §3.1) */
    /* the driver classes' codes, four per class (SPEC-017 §3, DRV-008); -96 to -127 later */
    EMB_EFRAMING = -64,   /* UART: a character without its stop bit */
    EMB_EPARITY = -65,    /* UART: a parity error */
    EMB_EOVERRUN = -66,   /* UART: bytes lost, the receiver or its ring full */
    EMB_ECRC = -68,       /* SPI: the hardware CRC did not match */
    EMB_EMODEFAULT = -69, /* SPI: another controller drove the bus */
    EMB_ENACK = -72,      /* I2C: the target did not acknowledge */
    EMB_EARBLOST = -73,   /* I2C: arbitration lost to another controller */
    EMB_EBUSERR = -74,    /* I2C: a misplaced start or stop on the bus */
    EMB_EXFER = -76,      /* DMA: the transfer failed (a bus or FIFO error) */
    EMB_EWINDOW = -80,    /* watchdog: refreshed outside its window */
    EMB_EWRPROT = -84,    /* flash: the sector is write-protected */
    EMB_EVERIFY = -85,    /* flash: what was read back differs from what was written */
    EMB_ENOTSET = -88,    /* RTC: the time was never set since power-up */
    EMB_EOSCFAIL = -89,   /* RTC: its oscillator stopped */
    EMB_EHEALTH = -92,    /* entropy: the source failed its health test */
    EMB_EAPP_BASE = -128  /* application-defined codes from here down to -255 */
};

/**
 * emb_status_name() - The symbolic name of a status code.
 * @status: Any status value.
 *
 * @ctx      thread isr prekernel
 * @blocks   no
 * @time     O(1)
 * @owns     none
 * @config   CONFIG_EMB_STATUS_NAMES
 * @req      API-011
 * @since    0.2
 * @stable   yes
 *
 * Return: "EMB_EINVAL" and the like; "EMB_E?" for an unknown value; "" when the
 *         names are compiled out.
 */
const char *emb_status_name(emb_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* EMB_STATUS_H */
