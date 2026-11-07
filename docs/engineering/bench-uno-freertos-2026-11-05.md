# EmbLinkRTOS and FreeRTOS on the Arduino Uno (2026-11-05)

The first measured comparison with another kernel (R-003 §6, the review's F14), on the same physical board, with the same benchmark source, compiler, flags and clock. One board, one compiler, one configuration each: a comparison, not a ranking.

## Method

- **Board:** the Smraza Arduino Uno R3 clone of the [hardware runs](hil-uno-2026-11-05.md), ATmega328P at 16 MHz.
- **Source:** `tests/benchmarks/bench_compare.c`, one file for both kernels. EmbLinkRTOS builds it through CMake (`bench_compare`). FreeRTOS builds it with `-DBENCH_FREERTOS` through `tools/bench/freertos-avr/build.sh`, which fetches FreeRTOS-Kernel **V11.1.0** at its pinned commit `dbf70559` and uses its ATmega port.
- **Compiler and flags:** avr-gcc 14.4.0 for both, with `-Os -mrelax -mcall-prologues -fno-short-enums`, the flags of `cmake/toolchains/avr-gcc.cmake`.
- **Configuration:**
  - EmbLinkRTOS: tiny profile, release (`EMB_CONF=configs/release.conf`) and checked, ticking at 1 kHz on Timer1.
  - FreeRTOS: `tools/bench/freertos-avr/FreeRTOSConfig.h`, matched to it. Static allocation only, no heap, preemption without time slicing, and 32-bit ticks. No assertions, stack-overflow checks, trace, hooks or software timers. The port's default tick is the watchdog at 15 ms; the build script moves it to Timer0 at about 1 kHz (1042 Hz), so both kernels pay a similar tick.
- **Clock:** Timer1 at F_CPU / 64, 250 kHz, a 4 µs unit, for both. EmbLinkRTOS reads it through `emb_arch_cycles()`. FreeRTOS reads Timer1 free-running with an overflow count, since its tick is on Timer0.
- **Each figure is a total over thousands of operations, divided by their number.** The tick's own interrupts are inside the totals for both kernels.
- **Threads:** a driver at priority 2 and an echo thread at priority 3. The interrupt is USART_UDRE, which fires as soon as UDRIE0 is set while the transmitter is idle; the benchmark waits for the transmitter to finish first.

| Measurement | What one operation is |
|---|---|
| `notify_handover` | a direct-to-thread notification to the other thread, which answers with one: one switch, averaged over 20,000 |
| `sem_handover` | a binary semaphore each way: one switch, averaged over 20,000 |
| `sem_give_take` | give then take on one thread, no switch: 10,000 pairs |
| `isr_wake` | the interrupt gives a semaphore and wakes the higher-priority thread, which counts and blocks again: interrupt entry, give, switch in, take, switch back, over 5,000 |

## Results

Per operation, in µs (4 µs × `per_op_x100` / 100). The records are under `hil-runs/2026-11-05-bench-compare/`.

| | FreeRTOS 11.1.0 | EmbLinkRTOS release | EmbLinkRTOS checked | EmbLinkRTOS release, before the fast paths below |
|---|---|---|---|---|
| `notify_handover` | 59.8 | 67.0 (+12%) | 82.8 | 68.8 |
| `sem_handover` | 65.6 | 66.0 (+1%) | 84.2 | 69.4 |
| `sem_give_take` | 16.5 | 22.1 (+34%) | 26.0 | 31.6 |
| `isr_wake` | 116.9 | 111.2 (−5%) | 161.8 | 114.0 |
| image flash | 8,110 B | 9,210 B (+14%) | 10,386 B | 9,044 B |
| image RAM (data + bss) | 898 B | 594 B (−34%) | 598 B | 594 B |

Read with the method:
- The RAM difference is mostly FreeRTOS's idle task stack (128 bytes) and its larger task control blocks. EmbLinkRTOS's idle context runs on the pre-kernel stack (ADR-036), and a tiny thread's control block is 35 bytes.
- FreeRTOS's semaphores are queues of length 1; EmbLinkRTOS's binary semaphore is a counting semaphore saturating at 1.
- The checked build adds the API checks, the stack-guard checks at every switch and at every outermost interrupt exit (A90), and the crash record.

## What changed in the kernel because of it

The first run had EmbLinkRTOS's give-and-take at 31.6 µs, twice FreeRTOS's. Profiling on the board, in units of 64 cycles, put a third of a `EMB_NO_WAIT` take in computing a deadline that a call that cannot block never uses: reading the 32-bit clock and the saturating arithmetic. Three fast paths followed (A94):
- `embk_deadline_if_blocking()`: the relative-timeout calls of semaphores, mutexes, condition variables, events, notifications and message queues compute no deadline for `EMB_NO_WAIT`;
- `embk_thread_take_cancel_locked()` is inline;
- `embk_sched_reschedule_if_needed()` is an inline test of the pending flag in front of the call.

Give-and-take fell to 22.1 µs (−30%), and the semaphore hand-over to within 1% of FreeRTOS. The notification hand-over stays 12% behind: EmbLinkRTOS blocks in three short critical sections where FreeRTOS uses one longer one (SPEC-004). That is also why its interrupt wake is the faster of the two.

## To repeat

```sh
cmake --preset avr-uno-hil -B build/cmp-release -DEMB_HIL_PORT=/dev/cu.usbmodem11101 -DEMB_CONF=configs/release.conf
cmake --build build/cmp-release --target bench_compare
tools/bench/freertos-avr/build.sh
for elf in build/freertos-avr/bench_compare.elf build/cmp-release/tests/benchmarks/bench_compare.elf; do
  python3 -I tools/hil/run_hil.py $elf --hw-json boards/arduino/arduino_uno/generated/hw.json \
      --port /dev/cu.usbmodem11101 --until BENCH_END --bench
done
```
