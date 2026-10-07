# Port: AVR (ATmega328P, `arch/avr`)

**Specification:** SPEC-012 (port), SPEC-011 (contract), SPEC-002 §13 and SPEC-003 §13 (AVR rows).
**Status (M1):** periodic tick, context switch, `EMB_ISR` prologue and epilogue, critical sections, interrupt mask table, idle, faults by halt or watchdog reset. Tickless mode, the `.noinit` crash record, and the generated linker script come with later milestones; the configuration refuses `CONFIG_EMB_TICKLESS` on this port until then.
**Boards:** `arduino_uno` (ATmega328P at 16 MHz, USART0 console at 115200, LED on PB5, Optiboot keeps the top 512 bytes of flash).
**Verified on:** QEMU `arduino-uno` machine (SIM-002): the first-execution sequence and the conformance suite. No hardware run yet: every figure below marked *measured* comes from the toolchain's output, not from a board, and no latency number is published (SIM-003).

## Context frame

One layout serves the switch, the interrupt epilogue, and the initial frame (SPEC-012 §4.1), 35 bytes from the saved stack pointer upward:

| offset from saved SP | content |
|---|---|
| +1 .. +30 | r31 down to r2 (the push order is r2 first, r31 last) |
| +31 | r1 (zero register) |
| +32 | SREG (I bit: the context's own state; forced set by the ISR prologue) |
| +33 | r0 |
| +34 | return address high byte |
| +35 | return address low byte |

The manifest budgets 37 bytes per context (`stack.context_frame`), two more than the frame for the return address of the call into the switch routine. The initial frame returns into `embk_thread_launch(entry, arg)` with `entry` in r24:r25 and `arg` in r22:r23, which calls `entry(arg)` and exits the thread with code 0 (KRN-THR-005).

## Switch and interrupt exit

- `emb_arch_switch_to(next)` is a naked routine: push the frame with `cli` after reading SREG, store SP into the outgoing thread's `arch.sp`, make `next` current, load its SP, restore, `ret`. Masked for its whole length: 35 pushes, 35 pops, and the pointer exchange, about 70 cycles at 16 MHz by instruction count (not yet measured on a board).
- `EMB_ISR(name)` defines `__vector_<n>` from the board's `EMB_VECTOR_<name>`: the same frame push with the I bit forced set in the saved SREG (`set; bld r0,7`, since `ori` needs r16 or above), `embk_isr_enter()`, the C body `embk_isr_body_<name>()`, `embk_isr_exit()`, an unconditional SP exchange with the thread the kernel named (the running one when nothing changed: no branch, no label, per SPEC-011 §15), restore, `ret`.
- **Deviation from SPEC-012 §4.4, recorded here:** the epilogue ends with `ret`, not `reti`. The restored SREG carries the resumed context's own I bit; `reti` would set I unconditionally and enable interrupts inside a switched-to thread's critical section. The interrupted context's saved SREG has I forced set, so `out SREG` followed by `ret` re-enables interrupts for it with the architecture's one-instruction delay (SPEC-012 §14 decision 2 relies on the same delay for the launch).
- `EMB_ISR_RAW(name)` is a plain `signal` handler that may not call the kernel.
- The kernel's interrupt-exit hook returns the context to resume, never NULL (an addition to SPEC-011 §6 made for this port's branch-free epilogue).

## Kernel start and idle

`emb_arch_kernel_start(first, idle)` makes the pre-kernel context the idle context: it switches to the first thread, saving `main()`'s context into the idle control block; when the scheduler later picks the idle context it resumes there, enables interrupts, and runs the kernel's idle loop (`sei; sleep` per SPEC-011 §9). The pre-kernel stack is the idle stack (SPEC-012 §3). The port passes both threads to the start function, a deviation from SPEC-011 §4's bare stack pointer recorded in `include/emb/arch.h`.

## Interrupts and critical sections

- `emb_arch_irq_lock()`: `in r, SREG; cli`; the key is the saved SREG. `unlock`: `out SREG, key`. Both always-inline with a `"memory"` clobber.
- Kernel-aware handlers run with I clear (no nesting); `CONFIG_EMB_AVR_ISR_STACK_RESERVE` (64 bytes) is the per-thread reserve for the 35-byte frame plus the handler's C frame.
- The mask table (`arch/avr/avr_port.c`) maps each of the 26 vectors to its interrupt-enable register and bit; `emb_irq_set_level` is a no-op (vector order is the priority), `emb_irq_pend` is `EMB_ENOTSUP`, and `emb_irq_clear_pending` covers the external, pin-change and timer flags.

## Time

Periodic tick on Timer1 CTC: prescaler `CONFIG_EMB_AVR_TIMER1_PRESCALER` (64), `OCR1A = F_CPU / prescaler / tick_hz - 1` (249 at 16 MHz and 1 ms), vector `TIMER1_COMPA`. The clock is read inside the critical section (SPEC-003 §3.1 AVR row). `emb_arch_cycles()` returns prescaled Timer1 counts rebuilt from the tick count and `TCNT1` (4 µs resolution at 16 MHz): coarse statistics only; hard timing is taken with a logic analyzer on a GPIO (SPEC-012 §14 decision 9).

## Memory and linking

- Registration tables (`emb_init_table`, `emb_test_table`) live in flash in `.progmem.<name>` sections, bracketed by `__start_`/`__stop_` symbols in the board's linker script (`boards/arduino/arduino_uno/arduino_uno.ld`, derived from the toolchain's avr5 script with the flash region ending below Optiboot). GCC for AVR lets the `progmem` attribute override an explicit section name, so table entries are declared with `EMB_TABLE_CONST` (plain `const` plus the section) and read with `EMB_FLASH_READ_*`.
- `const` tables of the kernel are `EMB_FLASH_CONST` (`progmem`) and read with `pgm_read_*`; string literals of a checked build (`__func__` locations) stay in RAM on avr-gcc 7, so the tiny profile sets `CONFIG_EMB_FAULT_WHERE=n`.
- Stack checks: the kernel's guard words (8 bytes at the stack limit) at every switch; no hardware limit, no MPU.

## Footprint (M1 baseline, avr-gcc 7.3 `-Os`, `samples/first_execution`, tiny profile)

Measured with `tools/footprint/footprint.py` from the link map; the kernel total counts the `kernel/` objects only.

| build | kernel text | kernel static RAM | thread control block | R-003 target |
|---|---:|---:|---:|---|
| checked | 5364 | 85 | 43 | — |
| release | 4577 | 84 | 42 | 4096 / 64 / 32 (T1, T2) |

The targets are not yet met. Candidates recorded for the footprint work: the idle context's full control block (43 bytes of the 85), the 64-bit conversion helpers linked for `EMB_MS()` at run time, the per-call context checks, and the control-block fields that the tiny profile does not need (`stack_size`, `exit_code` separate from `wake_data`, the object header padding). The CI gate (`tools/footprint/thresholds-avr-uno*.json`) is the baseline plus 10 percent until the targets are reached.

## Skipped conformance tests

Tests needing fork-based fault expectation (`EMB_ASSERT_FAULTS`, `EMB_ASSERT_MISUSE` in checked builds) are skipped on this port and reported as skips; the release build asserts the returned statuses instead. The 32-bit wrap test (2^31 ticks) runs in virtual time only. The dynamic-connect validation test needs `CONFIG_EMB_IRQ_DYNAMIC`.

## Toolchain notes

- avr-gcc cannot compute `-fstack-usage` for naked functions and errors out, so the flag is dropped for AVR builds; stack figures come from the call graph.
- EmbCC: untested in M1 (`cmake/toolchains/embcc-avr.cmake` exists); SPEC-012 §8 and §10 list what changes under it (`const` in RAM, no linker script, no DWARF).
