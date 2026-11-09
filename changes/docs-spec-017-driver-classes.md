### Added (the driver classes, 2026-11-09)

- SPEC-017, proposed: the eight driver classes M4 delivers on the STM32F4 and the RP2350 (UART, SPI, I2C, DMA, watchdog, flash, RTC, entropy). Each answers 04 §3.2's checklist, names its drivers and the evidence each needs, and has its status codes in a sub-range of −64 to −95; −96 to −127 stay for later classes. An absent capability is reported, never emulated: the STM32F446 has no entropy source and the RP2350 no calendar ([SPEC-017](docs/specs/SPEC-017-driver-classes.md)).
- The specifications' index lists every `SPEC-*` and `AMENDMENTS-*` page by pattern, so a new specification no longer edits it.
