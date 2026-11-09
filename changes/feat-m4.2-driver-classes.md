### Changed (request lanes, 2026-11-09)

- A114, proposed: a device has two request lanes, each its own first-in-first-out queue whose head starts whatever the other lane does, so a full-duplex device writes while it reads. A request names its lane (`emb_drv_request_t.lane`, 0 by default); a device that needs one lane behaves as before ([amendments](docs/specs/AMENDMENTS-M4.md)).

### Added (the driver classes' status codes, 2026-11-09)

- The status codes of SPEC-017 §3, four per class in −64 to −95: `EMB_EFRAMING`, `EMB_EPARITY`, `EMB_EOVERRUN` (UART), `EMB_ECRC`, `EMB_EMODEFAULT` (SPI), `EMB_ENACK`, `EMB_EARBLOST`, `EMB_EBUSERR` (I2C), `EMB_EXFER` (DMA), `EMB_EWINDOW` (watchdog), `EMB_EWRPROT`, `EMB_EVERIFY` (flash), `EMB_ENOTSET`, `EMB_EOSCFAIL` (RTC) and `EMB_EHEALTH` (entropy), each named by `emb_status_name()` (DRV-008).

### Added (the UART class, 2026-11-09)

- `<emb/drv_uart.h>`, behind `CONFIG_EMB_DRV_UART` (SPEC-017 §4): write and read requests that run at once, a write on one lane and a read on the other; `emb_drv_uart_write()` and `emb_drv_uart_read()`, blocking with a timeout, a read ending early when the line goes idle if it asks; `emb_drv_uart_poll_write()` and `emb_drv_uart_poll_read()` for the console before the kernel and the fault report. Drivers share its receive core: a ring that keeps the bytes no read is waiting for, the read in progress, damaged bytes ending a read with `EMB_EFRAMING` or `EMB_EPARITY`, and bytes lost to a full ring reported by the next read as `EMB_EOVERRUN`.
- `emb_device_caps()` returns a device's capability bits, from its driver's operations (SPEC-016 §11).
