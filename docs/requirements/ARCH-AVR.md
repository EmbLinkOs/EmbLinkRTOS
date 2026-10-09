# ARCH-AVR - ATmega328P Port

Group `ARCH-AVR`. Design: `docs/specs/SPEC-012-atmega328p-port.md`. Related groups: PORT (the contract), KRN-IRQ, KRN-TIM, KRN-SCH (table scheduler), KRN-MEM, FLT, SIM, TEST.

New group; numbering starts at 001.

---

### ARCH-AVR-001  Manifest and tiny defaults
**Statement.** The AVR port shall declare 8-bit words, 16-bit pointers, byte stack alignment, a 37-byte context frame, no nesting, no zero-latency class, no compare-and-swap, no interrupt stack, no protection, no fault entry, and tickless limited to idle modes; the tiny defaults (table scheduler, 32-bit time, 8-bit notifications, no system work queue, no idle thread) shall be mandatory on it.
**Rationale.** SPEC-012 §2; ADR-036.
**Status.** Accepted 2026-10-07
**Verification.** Manifest schema check; configuration test refusing the isolated profile on AVR.
**Trace.** SPEC-012 §2

### ARCH-AVR-002  One frame layout
**Statement.** The thread-context switch, the interrupt epilogue switch, and the initial frame shall use the single 37-byte layout of SPEC-012 §4.1 (return address, `r0`, `SREG`, `r1`, `r2..r31`), so that an interrupt-exit switch is an exchange of stack pointers.
**Rationale.** One switch path (PORT-004) and the cheapest possible P1 on a core without an interrupt stack.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/avr/frame_layout`, `arch/avr/switch_from_isr_and_thread_interoperate`.
**Trace.** SPEC-012 §4

### ARCH-AVR-003  Atomic launch and switch tail
**Statement.** The switch and launch sequences shall end in `out SREG` followed by `ret`, and the port shall verify with a test that an interrupt pending at that point is taken only after the `ret`.
**Rationale.** The documented one-instruction interrupt delay is relied upon; it must be demonstrated, not assumed.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/avr/launch_with_pending_interrupt`.
**Trace.** SPEC-012 §4.2, §4.3

### ARCH-AVR-004  Interrupt prologue, epilogue, and reserve
**Statement.** `EMB_ISR` shall save the full frame with `I` forced set in the saved `SREG`, run the body with interrupts masked, and switch at exit when a reschedule is pending and the scheduler is unlocked; `EMB_ISR_RAW` shall be a `signal` handler that may not call the kernel; every thread stack shall include `CONFIG_EMB_AVR_ISR_STACK_RESERVE` (default 64 bytes), documented per release from `-fstack-usage`.
**Rationale.** SPEC-002 §6.2, §7.2 on this core.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/avr/isr_frame`, `arch/avr/isr_reserve_check`; documentation review.
**Trace.** SPEC-012 §4.4, §5

### ARCH-AVR-005  Timer1 modes and latency
**Statement.** The default tick shall be an exact 1 ms Timer1 CTC interrupt (prescaler 64, `OCR1A = 249` at 16 MHz); tickless shall use Timer1 free-running at prescaler 256 with a 1.048 s `MAX_INTERVAL`; arming latency shall be measured at init; `emb_arch_cycles` shall be a prescaled 32-bit count with its resolution documented; deep sleep shall be `EMB_ENOTSUP` until a tick-loss policy exists.
**Rationale.** SPEC-003 §13; KRN-TIM-036.
**Status.** Accepted 2026-10-07
**Verification.** HIL test: tick exactness over one minute against the host clock; `arch/avr/tickless_hop`; latency measurement test.
**Trace.** SPEC-012 §7

### ARCH-AVR-006  Footprint budget
**Statement.** The tiny reference configuration on this port shall fit the T1 and T2 targets: kernel text at most 4 KB at `-Os` on EmbCC and avr-gcc, kernel static RAM at most 64 bytes, TCB at most 32 bytes; the footprint test shall enforce them from the map file.
**Rationale.** R-003 §4.
**Status.** Accepted 2026-10-07
**Verification.** Footprint test in CI on both compilers.
**Trace.** SPEC-012 §8, §13

### ARCH-AVR-007  Flash constants and linker outputs
**Statement.** Kernel tables shall use `EMB_FLASH_CONST` and its accessors so that avr-gcc places them in program memory while EmbCC builds still work; large tables shall be compiled out on tiny; the generator shall emit the `embld` option set and a GNU ld script for the board from one region model.
**Rationale.** 09 §2 (`const` in RAM), §7 (no linker scripts on AVR); HW-006.
**Status.** Accepted 2026-10-07
**Verification.** Footprint comparison EmbCC versus avr-gcc; generator test for both outputs.
**Trace.** SPEC-012 §8

### ARCH-AVR-008  Robustness without faults
**Statement.** Checked builds on this port shall enable the switch-out stack guard check, the interrupt-reserve check, and the software watchdog service; a kernel fault shall write the small crash record to `.noinit` and reset through the watchdog by default.
**Rationale.** No fault hardware; the watchdog and the guards are the only safety nets.
**Status.** Accepted 2026-10-07
**Verification.** Test: `arch/avr/guard_detects_overflow`, `arch/avr/crash_record_survives_wdt_reset`.
**Trace.** SPEC-012 §8, §9; FLT-001, FLT-004

### ARCH-AVR-009  Debug and emulation path
**Statement.** Symbolic debugging shall use the avr-gcc build until EmbCC emits DWARF on AVR; the EmbCC build shall be verified by the conformance suite under QEMU and on the `arduino_uno` board, and timing from emulation shall never be published.
**Rationale.** 09 §7 gap 4, §9; SIM-002, SIM-003.
**Status.** Accepted 2026-10-07
**Verification.** CI: QEMU AVR run of the conformance suite on both compilers; HIL run on the Uno.
**Trace.** SPEC-012 §10, §12

### ARCH-AVR-010  Board description
**Statement.** `boards/arduino/arduino_uno.yaml` shall describe the 16 MHz crystal, the USART0 console, the user LED, and the flash partitions leaving Optiboot's 512 bytes reserved; board init shall configure only the console and the LED.
**Rationale.** A new board on this SoC is a description file (01 §6).
**Status.** Accepted 2026-10-07
**Verification.** Generator test; boot test on the board.
**Trace.** SPEC-012 §11
