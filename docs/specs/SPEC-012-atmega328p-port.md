# SPEC-012 - ATmega328P Port (`arch/avr`) and the `arduino_uno` Board

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §14. Specification work item 11 of the roadmap (07 §3). First hardware target of M1 (ADR-001, alongside the native port).
**Requirements:** `docs/requirements/ARCH-AVR.md` (new group, from 001).
**Builds on:** SPEC-011 (the contract this port implements), SPEC-002 §6.2, §7.2, §13 (AVR rows), SPEC-003 §13 (Timer1), SPEC-004 §11, §12 (table scheduler, no compare-and-swap), SPEC-008 §10 (stacks), 03 §11 (tiny reference configuration), 09 §2, §5, §7, §8 (EmbCC on AVR: `const` in RAM, no linker scripts, no DWARF, `signal`/`interrupt` attributes, full AVR5 inline-asm vocabulary); R-003 §4 targets T1 and T2.
**Hardware:** ATmega328P: AVR5 core (AVRe+), 16 MHz on the Uno, 32 KB flash (Optiboot occupies the top 512 bytes), 2 KB SRAM at `0x0100..0x08FF`, 1 KB EEPROM, 26 interrupt vectors, Timer0 (8-bit), Timer1 (16-bit), Timer2 (8-bit), USART0, SPI, TWI, ADC, watchdog, six sleep modes. No MPU, no fault exceptions, no cycle counter.

---

## 1. Why this target first

A 2 KB SRAM part forces every "compiles to nothing" claim of the architecture to be true (03 §11, ADR-036, R-003 T1 and T2). What fits here fits everywhere; what does not is found before the Cortex-M port makes it easy to ignore. The port is also small enough to be written and reviewed in full in M1 and runs under QEMU's AVR machine in CI (SIM-002).

## 2. Manifest

```yaml
arch: avr
variants: [atmega328p]
word_bits: 8                    # 16-bit pointers; the kernel's "word" for bitmaps is 8 bits
endian: little
stack: { align: 1, growth: down, min_thread: 96, context_frame: 37 }
features:
  irq_nesting: false            # kernel-aware handlers run with I cleared
  zero_latency_irqs: []         # cli/sei is all or nothing
  cas: []                       # transitions in a critical section (SPEC-004 §12)
  interrupt_stack: false        # frames on the interrupted thread's stack; per-thread reserve
  hw_stack_limit: []
  mpu: none
  fpu_lazy: []
  cycle_counter: []             # Timer1 count serves as a coarse cycle source (§7)
  tickless: true                # idle and ADC-noise-reduction sleep only
  smp: false
  syscall_trap: none
  fault_entry: false
  priority_bits: none           # one interrupt priority level: vector order decides
tcb_extension_bytes: 0
```

Configuration consequences: the isolated profile, the zero-latency class, SMP, and the fault-based stack guard are refused; `CONFIG_EMB_SCHED_TABLE=y`, `CONFIG_EMB_TICK_32BIT=y`, `CONFIG_EMB_NOTIFY_BITS=8`, `CONFIG_EMB_SYSTEM_WORKQ=n`, `CONFIG_EMB_IDLE_THREAD=n` are the tiny defaults (ADR-036, 03 §11).

## 3. Startup

- Boot is the toolchain's `.S` sequence (09 §8): `SPH:SPL` set to `RAMEND`, `r1` cleared, `.data` copied from flash, `.bss` zeroed, then `main()`. Optiboot jumps to address 0 after its timeout, so the image is linked at `0x0000` with `--rom-limit 32256` (EmbCC) or the equivalent `MEMORY` length (avr-gcc), leaving the bootloader section alone.
- `emb_arch_early_init()`: `cli`; nothing else (no vector relocation on this part; `IVSEL` stays 0).
- `emb_arch_init()`: Timer1 configured per §7, sleep mode select left to `emb_arch_idle`, watchdog left to the board or the software watchdog service (FLT-004).
- `emb_arch_kernel_start(first_sp)`: loads `SP` from the first thread's frame and runs the restore sequence of §4 ending in `ret`; the pre-kernel stack (top of SRAM) becomes the inline idle context's stack (`CONFIG_EMB_IDLE_THREAD=n`, SPEC-008 §10) or the idle thread's stack when one is configured.

## 4. Context frame and switch

### 4.1 Frame

One layout serves the thread-context switch (P2), the interrupt epilogue switch (P1), and the initial frame, growing down from the return address the hardware or the call pushed:

```
higher addresses
  return address low byte      (pushed by call/interrupt: low byte at the higher address)
  return address high byte
  r0
  SREG                         (saved through r0: in r0, SREG)
  r1                           (always 0 for compiled code)
  r2 .. r31                    (r24:r25 carry the entry argument in the initial frame)
lower addresses  <- saved SP (points to the free byte below r31)
```

37 bytes per saved context (2 return address, 32 registers, 1 `SREG`, 2 for the exit address in the initial frame counted once in `stack.min_thread`). `EMB_ISR` saves exactly this frame in its prologue, so an interrupt epilogue that decides to switch only stores `SP` into the outgoing TCB and loads the incoming one: the ISR frame *is* the context frame.

### 4.2 Initial frame

`emb_arch_context_init(stack_base, size, entry, arg, exit_fn, privileged)` (privilege ignored) writes, from the top of the stack: `exit_fn` (low, high) so that `entry` returns into it (KRN-THR-005); `entry` (low, high) as the launch return address; `r0 = 0`; `SREG = 0x80` (I set); `r1 = 0`; `r2..r23 = 0`; `r24:r25 = arg`; `r26..r31 = 0`; returns the saved `SP`. Launch is the normal restore (§4.3), whose final `out SREG` enables interrupts; the AVR core executes the following instruction (`ret`) before taking any pending interrupt, so the launch is atomic. A test verifies this with an interrupt pending at launch.

### 4.3 Switch routine

`emb_arch_switch_to(next)` is a `naked` function (09 §5): push `r0`; `in r0, SREG`; `cli`; push `r0`; push `r1`; `clr r1`; push `r2..r31`; store `SP` into `current->arch.sp`; load `SP` from `next->arch.sp`; set `current`; pop `r31..r2`; pop `r1`; pop `r0`; `out SREG, r0`; pop `r0`; `ret`. Interrupts are masked from the `cli` until the `out SREG` of the incoming thread restores its own `I` bit (an incoming thread saved by an `EMB_ISR` has `I` clear in its saved `SREG`? No: the ISR prologue saves `SREG` *as the interrupted code had it*, with `I` set, because the hardware clears `I` only in the live register; the saved copy is read from `SREG` after entry, so the port explicitly sets bit 7 in the saved byte). The whole routine is the masked section; its length is fixed (about 70 cycles at 16 MHz, about 4.4 µs) and is the port's contribution to the T7 budget on this target.

### 4.4 Interrupt prologue and epilogue

`EMB_ISR(vector)` expands to a `naked` `__vector_n` whose body is: the frame push of §4.3 with `I` forced set in the saved `SREG` (interrupts are off in the live register for the handler's duration, SPEC-002 §7.1: no nesting), `irq_nesting_depth++`, a call to the C body `embk_isr_<vector>()`, then `embk_isr_exit()`, which decrements the depth and, when `reschedule_pending && sched_lock_depth == 0`, performs the SP exchange of §4.3 (the frame is already on the stack); then the restore sequence ending in `reti`. `EMB_ISR_RAW(vector)` is `__attribute__((signal))` on both compilers: the compiler saves only what the body clobbers, the handler may not call the kernel (SPEC-002 §3), and it runs with interrupts off.

### 4.5 Timer interrupt

The Timer1 compare vector is an `EMB_ISR` whose body calls `embk_time_timer_isr()` (SPEC-003 §4); expiry wakes threads through the wait protocol in the same masked section; the epilogue switches if needed. The whole tick path (prologue, tick update, one expiry, epilogue with switch) is measured and published (SPEC-002 §10).

## 5. Interrupts and critical sections

- `emb_arch_irq_lock()`: `in r, SREG; cli`; key = saved `SREG`. `emb_arch_irq_unlock(key)`: `out SREG, key`. Both `always_inline` with a `"memory"` clobber. `emb_arch_irq_locked()`: `SREG.I == 0`.
- `emb_arch_in_isr()`: the kernel's `irq_nesting_depth` (there is no hardware indicator).
- Vector numbers and names come from the SoC description (`soc/microchip/atmega/atmega328p.yaml`); `EMB_ISR(TIMER1_COMPA)` resolves to `__vector_11` through the generated `hw_config.h`. Unused vectors keep the toolchain's weak default, which the port overrides with a handler that records a `spurious_irq` fault.
- `emb_arch_irq_enable/disable(irq)` set or clear the peripheral's mask bit through a generated table of `(register, bit)` pairs; `set_level` is a no-op (one level; vector order is the hardware priority and the documentation says so); `clear_pending` writes the flag bit; `pend_soft` is unsupported (`EMB_ENOTSUP`).
- The per-thread interrupt reserve `CONFIG_EMB_AVR_ISR_STACK_RESERVE` defaults to 64 bytes: the 37-byte frame plus the largest kernel-aware handler's C frame measured with `-fstack-usage` (the port's `docs/ports/avr.md` lists it per release).

## 6. Scheduler and wait protocol on this port

The tiny defaults make the kernel's hot paths bit operations: the ready set and every wait queue are one byte (ADR-036, SPEC-004 §11), `wait_first` is a lookup-table or `clz` emulation over 8 bits (one instruction sequence of about 6 cycles), unique priorities 1..7 plus idle. Wait-state transitions run inside the critical section (no compare-and-swap, SPEC-004 §12); the three sections of SPEC-004 §5.1 are preserved, so interrupts are enabled between them.

## 7. Time

| Mode | Configuration | Resolution | Range |
|---|---|---|---|
| periodic tick (default) | Timer1 CTC, prescaler 64, `OCR1A = 249` at 16 MHz | 1 ms tick, exact | 32-bit tick counter (`CONFIG_EMB_TICK_32BIT`) |
| tickless | Timer1 normal mode free-running, prescaler 256, `OCR1A` as the deadline compare | 16 µs | `MAX_INTERVAL` 1.048 s (half the 16-bit range, SPEC-003 §4); longer sleeps hop |

- `emb_arch_timer_now_raw()` reads `TCNT1` (16-bit, atomic read through the temporary register with interrupts masked); the kernel's 32-bit tick is maintained by `embk_time_on_wake` folding elapsed raw ticks (SPEC-003 §4).
- Arming latency is measured once at `timer_init` (program a compare two counts ahead, count the cycles to the interrupt) and reported through `emb_arch_timer_set_latency_ticks()` (KRN-TIM-036); on this core it is a constant the documentation also states.
- `emb_arch_cycles()` returns a 32-bit count built from `TCNT1` and a software overflow counter at the prescaled rate (not true CPU cycles; the documentation states the resolution and the harness metadata records it). Hard timing numbers for this port are taken with an external logic analyzer on a GPIO, which the harness supports (R-003 §6).
- Deep sleep stops Timer1; tickless is limited to `IDLE` and `ADC noise reduction` modes (SPEC-003 §13). The power core's `emb_arch_sleep(SLEEP)` and deeper are `EMB_ENOTSUP` on this part unless an external wake source and a tick-loss policy are configured (FUTURE).
- Clock reads use the critical section (KRN-TIM-016 table).

## 8. Memory

- SRAM budget for the tiny reference configuration (R-003 T1, T2): kernel static RAM at most 64 bytes; TCB at most 32 bytes (saved `SP` 2, state and priority 2, wait state and generation 2, wait queue pointer 2, wake result and data 3, notification bits 1, timeout deadline 4, timeout links 4, owned list 2, flags 1, base priority 1); eight threads with 128-byte stacks use 1 KB; about 700 bytes remain for the application. The footprint test of the tiny configuration enforces these numbers from the map file.
- **`const` in RAM under EmbCC** (09 §2, gap 3): every `const` table costs SRAM. The portability layer offers `EMB_FLASH_CONST` and `EMB_FLASH_READ_U8/U16/PTR` (`PROGMEM` and `pgm_read_*` on avr-gcc, plain `const` and direct reads on EmbCC); kernel tables that are large (vector name table, status names) are compiled out on tiny; small ones (the interrupt mask table, init table) are accepted and counted. The footprint budget is stated for both compilers.
- **No linker script under EmbCC** (09 §7): the generator emits the `embld` option set (`-Ttext=0x0 -Tdata=0x800100 --rom-limit=32256 --gc-sections`) and a GNU ld `MEMORY`/`SECTIONS` script for avr-gcc from the same region model (HW-006); the init table and other registries use bracketed sections, which both linkers provide.
- Stack checking: guard words at each thread stack's limit checked at every switch-out (`CONFIG_EMB_STACK_CHECK`, default on in checked builds); the inline idle context's stack is the pre-kernel stack and gets the same guard. There is no hardware limit and no MPU.
- Retained region: `.noinit` SRAM survives a watchdog or external reset (not a power cycle); the small crash record (`EMB_CRASH_RECORD_SMALL`: magic, reason, thread index, `SP`, return address, uptime, checksum) lives there (FLT-001 on this target).

## 9. Faults and robustness

There is no fault entry: an illegal memory access is silent, a stack overflow is caught only by the guard check at the next switch, and a runaway thread only by the watchdog. The port therefore enables, by default in checked builds: the stack guard check at every switch, the ISR-stack reserve check at every kernel-aware exit, and the software watchdog service (FLT-004) feeding `WDT` with a 1 s window; a checked-build kernel fault writes the small crash record and resets through the watchdog (`CONFIG_EMB_FAULT_ACTION=REBOOT` default on this target because there is no debugger halt path under EmbCC).

## 10. Debug and tooling

- EmbCC emits no DWARF on AVR (09 §7, gap 4) and `embdbg` has no AVR register layout (gap 5): symbolic debugging uses the avr-gcc build with `avr-gdb` and `simavr` or QEMU's gdb stub; the EmbCC build is verified by the same conformance suite and by the debug descriptor, which on this port carries the TCB layout, frame layout, and table addresses explicitly (SPEC-011 §14).
- Flashing: `avrdude -c arduino -p m328p -P <port> -b 115200` behind `emb flash` (BLD-005); no EmbFlash yet (09 §9).
- Emulation: QEMU `arduino-uno` machine (EmbCC's AVR harness, 09 §9) runs the kernel conformance suite in CI; timing results from it are never published (SIM-003).
- Trace: the tiny profile's trace is off by default; the plain-text log backend over USART0 at 115200 is the diagnostic channel (ADR-009 fallback, 09 §7).

## 11. Board `arduino_uno`

```yaml
board: arduino_uno
soc: microchip/atmega/atmega328p
oscillators: { main: { hz: 16000000, source: external_crystal } }
console: { device: usart0, baud: 115200, pins: { tx: PD1, rx: PD0 } }
leds: [ { name: user, gpio: PB5 } ]
flash_partitions: [ { name: app, base: 0x0000, size: 32256 }, { name: bootloader, base: 0x7E00, size: 512, reserved: true } ]
defaults: { CONFIG_EMB_PROFILE: tiny, CONFIG_EMB_TICK_NS: 1000000 }
```

Board init configures `USART0` for the console and `PB5` as output; nothing else (pins default to inputs). The generator emits `hw_config.h` with vector numbers, register addresses, and the SRAM and flash regions; the same SoC file serves any other ATmega328P board with a different crystal or console.

## 12. Conformance

`tests/arch/avr`: frame layout and launch with a pending interrupt, `EMB_ISR` prologue and epilogue switch, critical section key semantics, Timer1 tick exactness over one minute against the host clock (HIL), tickless hop across `MAX_INTERVAL`, arming latency measurement, guard-word detection, `.noinit` crash record survival across a watchdog reset. The full kernel conformance suite runs on QEMU and on the Uno; tests needing faults, isolation, FPU, nesting, or zero-latency interrupts are skipped by manifest, and the skip list is part of the port documentation.

## 13. Footprint and latency targets for this port

| Metric | Target | Source |
|---|---|---|
| kernel text, tiny configuration, EmbCC `-Os` and avr-gcc `-Os` | <= 4 KB | R-003 T1 |
| kernel static RAM | <= 64 bytes | R-003 T2 |
| TCB | <= 32 bytes | R-003 T2, §8 |
| context switch (P2, `emb_arch_switch_to`) | <= 80 cycles (5 µs) | §4.3 |
| interrupt to thread (Timer1 compare to the woken thread's first instruction) | <= 300 cycles (19 µs) | SPEC-002 §10 |
| longest masked section in kernel paths | <= 150 cycles | R-003 T7 |

Targets become baselines after the first HIL measurement (OBS-012).

## 14. Decisions taken at acceptance (2026-10-07)

1. One 37-byte frame layout for the switch, the interrupt epilogue, and the initial frame; the `EMB_ISR` frame is the context frame, so an interrupt-exit switch is an `SP` exchange.
2. Launch and switch end in `out SREG` then `ret`, relying on the documented one-instruction interrupt delay; a test with a pending interrupt verifies it.
3. Periodic 1 ms tick on Timer1 CTC with prescaler 64 is the default; tickless uses prescaler 256 with 16 µs resolution and a 1.048 s `MAX_INTERVAL`; deep sleep modes are unsupported until a tick-loss policy exists.
4. Tiny defaults are mandatory on this port: table scheduler, 32-bit time, 8-bit notifications, no system work queue, no idle thread, 7 application priorities.
5. `EMB_FLASH_CONST` and its accessors are the portability answer to `const` in RAM; large kernel tables are compiled out on tiny.
6. The generator emits both the `embld` option set and a GNU ld script for this board from one region model.
7. The small crash record lives in `.noinit` and the default fault action is a watchdog reset; the software watchdog service is on in checked builds.
8. Symbolic debugging uses the avr-gcc build until EmbCC emits DWARF on AVR; the EmbCC build is verified by conformance and by the descriptor.
9. Hard timing on this port is measured with a logic analyzer on a GPIO; the Timer1-based cycle counter is for coarse statistics.
