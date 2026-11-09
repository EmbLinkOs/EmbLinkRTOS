/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* The UART class on the board's own console (SPEC-017 §4; DRV-004, DRV-006, DRV-007): its
 * driver's interrupt-driven writes, a read that times out on a line nobody types on, and
 * the polled calls, on every board whose console is a UART device. */
#include <emb_test.h>
#include <soc_devices.h>
#include <string.h>

static const char line[] = "uart: a write by request, sent by the driver's interrupts\n";

EMB_TEST(uart_console_writes_by_request)
{
    size_t done = 0u;
    EMB_TEST_REQ("DRV-004", "DRV-005");
    EMB_ASSERT_TRUE(emb_device_ready(EMB_SOC_CONSOLE_DEVICE));
    EMB_ASSERT_OK(emb_drv_uart_write(EMB_SOC_CONSOLE_DEVICE, line, sizeof(line) - 1u,
                                     EMB_TIMEOUT(EMB_MS(1000)), &done));
    EMB_ASSERT_EQ(done, sizeof(line) - 1u);
    EMB_ASSERT_OK(emb_drv_uart_write(EMB_SOC_CONSOLE_DEVICE, line, 0u, EMB_NO_WAIT, &done));
    EMB_ASSERT_EQ(done, 0u);
}

EMB_TEST(uart_console_read_times_out)
{
    uint8_t in[4];
    size_t got = 9u;
    EMB_TEST_REQ("DRV-006");
    EMB_ASSERT_STATUS(emb_drv_uart_read(EMB_SOC_CONSOLE_DEVICE, in, sizeof(in), 0u,
                                        EMB_TIMEOUT(EMB_MS(20)), &got),
                      EMB_ETIMEDOUT);
    EMB_ASSERT_EQ(got, 0u);
}

EMB_TEST(uart_console_break_or_not_supported)
{
    emb_drv_uart_req_t brk;
    emb_status_t want = ((emb_device_caps(EMB_SOC_CONSOLE_DEVICE) & EMB_DRV_UART_CAP_BREAK) != 0u)
                            ? EMB_OK
                            : EMB_ENOTSUP; /* SPEC-016 §11: a capability lacked, never emulated */
    EMB_TEST_REQ("DRV-004");
    emb_drv_uart_req_break(&brk);
    /* a break may end inside start() or later, by the driver's timer: wait for it either way */
    EMB_ASSERT_STATUS(emb_drv_wait(EMB_SOC_CONSOLE_DEVICE, &brk.req, EMB_TIMEOUT(EMB_MS(200))),
                      want);
}

EMB_TEST(uart_console_polled_write)
{
    static const char polled[] = "uart: a polled write\n";
    EMB_TEST_REQ("DRV-007");
    EMB_ASSERT_OK(emb_drv_uart_poll_write(EMB_SOC_CONSOLE_DEVICE, polled, sizeof(polled) - 1u));
}
