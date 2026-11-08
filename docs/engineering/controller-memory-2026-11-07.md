# The reference controller's memory, accounted for (2026-11-07)

M3a's exit asks for "full memory/stack and measured latency evidence". The [timing report](controller-timing-2026-11-07.md) gives the latency. This report gives the memory: every byte of flash and RAM in the controller's image, and for each stack a static worst-case bound beside what the board measured.

The bound comes from the image's call graph and GCC's per-function frame sizes. Each stack holds well over its bound, and each measured high-water mark lies under it:

| Stack | Size | Static bound | Measured on the board | Bound's margin |
|---|---:|---:|---:|---:|
| control thread | 1,024 B | 484 B | 236 B | 540 B |
| command thread | 1,024 B | 532 B | 284 B | 492 B |
| telemetry thread | 1,024 B | 612 B | 340 B | 412 B |
| interrupt stack | 2,048 B | 1,604 B | 368 B | 444 B |

**Image:** `examples/controller` at `27c11d6` (the image of the timing report), GCC 14.2.1, the default checked build, for the NUCLEO-F446RE. The NUCLEO-F446ZE's image differs by 32 bytes of flash and has the same bounds.

**Tools:**
- `tools/footprint/memory_report.py` accounts for flash and RAM from the linker map and the image's sections and symbols.
- `tools/footprint/stack_bound.py` computes the stack bounds from the image and the build's `.su` files.
- `examples/controller/stack-roots.json` names the threads' entries, the interrupt levels and the targets of the indirect calls.
- CI runs both tools on both boards' builds, and fails when a bound exceeds its stack or the accounting does not reconcile.

## The regions

| Region | Size | Used | Free |
|---|---:|---:|---:|
| flash | 524,288 B | 18,096 B (3.5%) | 506,192 B |
| RAM | 131,072 B | 7,624 B (5.8%) | 123,448 B |

Flash holds the code, the constants and the initial values of `.data`. RAM holds the stacks, the static data and the area above them that the linker script keeps for the crash record. The image has no heap: no allocator is linked.

## Flash, by component

| Component | Bytes |
|---|---:|
| the application (`main.c`) | 3,113 |
| the Cortex-M port (`arch`) | 1,710 |
| kernel: threads | 1,625 |
| kernel: mutexes | 1,429 |
| kernel: message queues | 1,331 |
| kernel: crash record | 1,205 |
| kernel: scheduler | 1,067 |
| C runtime (libgcc and newlib-nano: division, `memset`, `strchr`, `strcmp`) | 874 |
| STM32 drivers (USART, RCC, GPIO, TIM, ADC, IWDG) | 842 |
| kernel: waits | 811 |
| kernel: time | 665 |
| kernel: execution (init, contexts) | 576 |
| board (`soc/st/stm32f4/board.c`, generated pin table) | 388 |
| kernel: software timers | 280 |
| kernel: faults | 262 |
| kernel: interrupts | 186 |
| kernel: work queues | 88 |
| kernel: notifications | 82 |
| kernel: build identity | 20 |
| string constants after merging, vector table alignment, linker tables | 1,542 |
| **total** | **18,096** |

The kernel's share is 9,627 bytes, about 53% of the image. The checked build keeps every API check and fault path; the [LTO report](perf-lto-2026-11-07.md) measured the release build with LTO 18% smaller.

The map gives each mergeable string constant its size before the linker merged duplicates, 2,929 bytes in all. So the strings are counted in the last line, after merging, not per component.

## RAM, by purpose

| Purpose | Bytes | What |
|---|---:|---|
| thread stacks | 3,328 | control, command and telemetry, 1,024 each; the idle thread's 256 |
| interrupt stack | 2,048 | MSP, `CONFIG_EMB_ISR_STACK_SIZE` |
| kernel state | 1,141 | the interrupt handler table (776), ready lists (256), time, scheduler and thread bookkeeping |
| thread control blocks | 480 | four of 120 bytes, the idle thread's included |
| kernel objects and queue rings | 296 | the `samples` and `rx_bytes` queues' objects and rings (8 × 8 and 64 × 1 bytes), the console mutex (40) |
| retained crash record area | 192 | above the stacks, kept across reset |
| application state | 99 | the shared counters (60), the settings and the objects' handles |
| port state | 16 | the switch's running and next thread, SysTick's reload |
| alignment padding | 24 | |
| **total** | **7,624** | |

Stacks account for 5,376 bytes, 71% of the RAM used.

## How the stack bound is computed

1. **The graph.** The call graph is read from the linked image's disassembly: each `bl`, each branch to another function's start (a tail call), and each `blx` through a register. Reading the image rather than the compiler's call graph catches the port's assembly: the switch, the fault entry and the vectors.
2. **Each function's frame.** This is GCC's `-fstack-usage` figure, which every build writes. For code without one (naked functions, the C library), the tool uses what the function's prologue pushes and subtracts. No frame in the controller is dynamic, and the graph has no recursion.
3. **Indirect calls.** The kernel makes four kinds:
   - The interrupt dispatcher calls the handlers `main()` connects: `adc_isr` and `usart_isr`.
   - `emb_kernel_init()` calls the initializers of the linker table `emb_init_table`. The tool reads their six addresses from the image itself.
   - The fault path calls a fault hook, which the controller never sets.
   - A software timer calls its callback, and the controller has none.
   Any other indirect call reachable from a root fails the analysis.

   **Where the call sits.** The roots file names the function that makes each indirect call in the source, such as `embk_isr_dispatch`. When the compiler inlines that function, the call sits in another one: under LTO, `embk_isr_dispatch` is inlined into `embn_cm_irq_entry`. Since 2026-11-08 the tool asks the debug information which functions are inlined at each indirect call (`addr2line -i`), innermost first, and uses the first one the roots file names. A function GCC cloned (`raise_info.constprop.0`) uses its original's entry. A call that no inlined function resolves still fails the analysis. Before this, the LTO image's bound could not be computed ([release builds](controller-release-2026-11-08.md)).

   **What is code.** A function's instructions end at its symbol's size. Clang places string constants after the code in the same section, and `objdump` disassembles them as the last function's instructions. As data, one decoded as an indirect branch in `embn_cm_pendsv` of the checked Clang image.
4. **A thread's bound.** Its deepest chain, plus what preemption leaves on its stack:
   - The exception's hardware frame: 32 bytes, or 104 with an active floating-point context.
   - The registers the switch saves: 36 bytes, or 100 with s16–s31.
   - 4 bytes of alignment.
   The telemetry thread reaches floating-point instructions (in `telemetry()`, GCC holds a 64-bit constant in `d8`), so it is charged the floating-point frames: 208 bytes.
5. **The interrupt stack.** One handler per priority level can be active at once. The levels, lowest first, are PendSV (the switch), the peripheral interrupts (ADC1 and USART2, one priority), SysTick, the faults and NMI. Each level nested above the first adds its own hardware frame. The tool charges 108 bytes per frame, since PendSV's code holds floating-point instructions. `main()`'s frames do not overlap the handlers: interrupts stay masked until the kernel starts, and the first switch resets MSP to its top (A90).

### The deepest paths

Every deepest path ends in the fault path: an API or invariant check that fails calls `embk_fault_raise()`. That raises the fault, writes the crash record (120 bytes of frame) and resets. The checked build keeps these checks on every kernel path, so the worst case is a fault at the deepest point, not normal operation. For the control thread:

```
control (56) -> emb_msgq_receive (40) -> receive_common (40) -> embk_block_on (24)
-> embk_wait_commit (24) -> embk_sched_switch_away (8) -> embk_sched_select (16)
-> embk_sched_remove_ready (8) -> embk_fault_raise (24) -> raise_info (32)
-> embk_crash_record_write (120) -> emb_arch_crash_regs (20)          = 412, + 72 preemption
```

For the interrupt stack, the deepest nesting is PendSV, then ADC1's handler sending to a queue whose check faults, then SysTick waking a thread, then a fault:

| Level | Chain | Frame | Total |
|---|---:|---:|---:|
| PendSV | 0 | (on the thread's stack) | 0 |
| ADC1 / USART2 | `embn_cm_irq_entry` → `adc_isr` → `emb_msgq_send` → … → the fault path: 412 | 108 | 520 |
| SysTick | `embn_cm_systick` → `embk_time_timer_isr` → … → the fault path: 352 | 108 | 460 |
| faults | `embn_cm_fault_entry` → `embn_cm_fault` → … → the crash record: 204 | 108 | 312 |
| NMI | `embn_cm_nmi` → `embk_fault_raise_hw` → … : 204 | 108 | 312 |
| **nested total** | | | **1,604** |

At boot, before the kernel starts, `main()` and the thread initializers use at most 508 bytes of the same stack.

## Measured high-water marks

The controller's `status` command reads each stack's high-water mark (`CONFIG_EMB_STACK_STATS`).
- **Timing image:** the table above comes from the timing report's image, read after 809 s that ran every timing scenario, 1 kHz and overload included.
- **Soak image:** the [controller's run](controller-nucleo-2026-11-07.md), on the image before the marker pins, read 236, 284, 348 and 376 bytes before its overload.

A measured mark above its bound would show the analysis wrong; none is. The gap between them is the fault path and the full nesting, which a healthy run never reaches.

## Not shown here

- **Build:** the checked build only. A release or LTO build drops most fault-path frames and will bound lower.
- **Library code:** its frames come from the prologue, which covers what it pushes and subtracts. None of the library functions here adjusts `sp` later.
- **Measurement under faults:** the measured marks come from runs without a fault at the deepest point. The bound includes it; a measurement of it would need a fault injected at that point.
- **Other applications:** the roots file is the controller's. Another application needs its own: its threads, its connected handlers, and its hook.

## Reproducing it

```sh
cmake -S examples/controller -B build/controller -G Ninja -DEMB_BOARD=nucleo_f446re
cmake --build build/controller
python3 tools/footprint/memory_report.py build/controller/controller.elf.map --elf build/controller/controller.elf
python3 tools/footprint/stack_bound.py --elf build/controller/controller.elf --build build/controller \
        --roots examples/controller/stack-roots.json
```
