# The debug descriptor

A debugger stopped on an EmbLink image sees one context: whatever the core was running. To show the other threads, it needs to know where the kernel keeps them, what each field of a thread control block means, and where a switched-out thread's registers went. The debug descriptor tells it, from the image itself (SPEC-015 §5, ADR-011, OBS-008, PORT-012, amendment A107).

Every image built with `CONFIG_EMB_DEBUG_DESCRIPTOR` exports a read-only table at the symbol `emb_debug_descriptor`. A tool reads it, then reads the kernel's memory where it points. The tool needs no copy of the kernel's private headers, so one tool serves every kernel version and configuration.

## What it holds

The format is in `include/emb/debug.h`. A 16-byte header comes first:

| Bytes | Field | |
|---|---|---|
| 0-3 | magic | `EMBD`, so a scan of memory finds it without symbols |
| 4-5 | version | 1; it changes only if the header does |
| 6-7 | count | the values that follow |
| 8 | pointer size | 2, 4 or 8: the width of each value and of every address |
| 9 | byte order | 0 little, 1 big |
| 12-15 | flags | what the build keeps: checked, the tiny scheduler table, the thread list, names, the object lists, stack fill, guard words, a packed index, an idle thread |

Then come `count` values, each one pointer-sized, by index (`EMB_DEBUG_*`):

- **Identity:** the kernel version, the address of the 20-byte build identity (A72), and the port's name and variant (`cortex_m`, `armv7em`).
- **The per-CPU state:** its address and the offsets of the running thread, the interrupt nesting depth, the scheduler lock depth, the pending reschedule and the kernel's state.
- **Where the threads are:** the all-threads list, or on the tiny profile the table of one thread per priority, and the idle context.
- **A thread control block's fields:** the saved stack pointer, type, state, flags, name, base and effective priority, wait state and reason, lifecycle flags, index, wait queue, deadline, first owned mutex, stack base and size, and notification bits.
- **A mutex's fields:** name, owner, the next mutex its owner holds, and recursion count; also the list of all mutexes.
- **The object type names**, by type tag.
- **Stacks:** the fill byte, the guard size, and the interrupt stack.
- **The port's switch frame**, described below.

A value that does not apply to the build is all ones (`EMB_DEBUG_ABSENT`): the thread list on the tiny profile, names when they are compiled out, the mutex fields without mutexes.

Indexes are append-only. A later kernel adds values at the end and raises `count`, and never moves or redefines one. A reader treats a value past `count` as absent, so a new reader serves an old kernel, and an old reader still reads a new kernel's first values.

### The switch frame

Each port describes what its context switch leaves on a thread's stack, in `arch/<port>/include/emb_arch_debug.h`. The description is:

- a kind;
- flags;
- the frame's size;
- a table of pairs (GDB's register number, word of the frame).

| Port | Kind | The frame from the saved sp |
|---|---|---|
| Cortex-M | `CORTEX_M` | PSPLIM on Armv8-M Mainline, then r4 to r11 and EXC_RETURN (the table). Then s16 to s31 when EXC_RETURN bit 4 is clear, then the hardware frame: r0 to r3, r12, lr, pc, xPSR, and s0 to s15 and FPSCR when it is the extended one. Then one aligner word when xPSR bit 9 is set. The thread's sp is what follows. |
| RISC-V | `TABLE` | The 128-byte frame of `rv_switch.S`: mepc (the pc), ra, t0 to t6, s0 to s11 and a0 to a7, each at its word. The thread's sp is the saved sp plus 128. |
| native | `NONE` | Native threads are host threads: the host debugger shows them itself. |
| AVR, Xtensa | `NONE` | Not described yet. |

## The tools

`tools/descriptor/descriptor.py` reads the descriptor through any memory reader. It lists the threads with:
- their state and wait reason, by the same rule as `emb_thread_state()`;
- their priorities;
- their stack: what is in use at the switch, and the high-water mark when stacks are filled (`CONFIG_EMB_STACK_STATS`);
- the mutexes each owns;
- the registers a switched-out thread's frame holds.

### With GDB

`tools/descriptor/emb_gdb.py` adds three commands to GDB:

```
(gdb) source tools/descriptor/emb_gdb.py
(gdb) emb threads
idx  name        state              prio  stack now/max/size  sp          pc      owns
0    runner      running            1->5  -/-/4096            0x20001e54  -       dd_lock
1    dd_sleeper  blocked (sleep)    4     148/-/1024          0x20000c24  0x2f1a  -
2    dd_parked   blocked (suspend)  3     108/-/2048          0x200027ec  0x2f1a  -
3    dd_fpu      blocked (sleep)    6     292/-/2048          0x20002f34  0x2f1a  -
4    dd_waiter   blocked (object)   5     180/-/2048          0x200037a4  0x2f1a  -
254  idle        ready              0     68/-/256            0x20000d74  0x1234  -
(gdb) emb bt 3
#0  0x00002f1a in open_window () at arch/cortex_m/cm_port.c:86
#1  emb_arch_switch_to (next=0x20001fa0 <runner_storage>) at arch/cortex_m/cm_port.c:164
#2  0x00002674 in embk_sched_switch_away () at kernel/sched/sched.c:413
#3  0x00002872 in embk_wait_commit (...) at kernel/wait/wait.c:132
#4  0x000028f0 in embk_block_on (...) at kernel/wait/wait.c:151
#5  0x00000efc in sleep_until_deadline (...) at kernel/thread/thread.c:674
#6  0x00001608 in emb_thread_sleep (d=...) at kernel/thread/thread.c:688
#7  0x000003fc in fpu_user (arg=<optimized out>) at tests/conformance/test_debug_descriptor.c:204
#8  0x00001242 in embk_thread_launch (...) at kernel/thread/thread.c:350
#9  0x00000000 in ?? ()
```

This listing is from `test_debug_descriptor` on QEMU's MPS2-AN386 (Cortex-M4F), stopped in `debug_descriptor_checkpoint()`:
- The runner holds `dd_lock`, so it inherits priority 5 from `dd_waiter`, which waits for that lock.
- `dd_fpu` used the FPU before it slept, so its frame is the floating-point one: 292 bytes in use against the sleeper's 148.
- `emb bt 3` loads the registers that `dd_fpu`'s switch saved, runs `bt`, and puts the core's own registers back. Frame 0 is where PendSV took the thread: the instant its switch opened the critical section. The `??` at the end is the thread's root: its initial frame has a zero return address.

The other commands:
- `emb bt INDEX FRAMES` caps the backtrace, which is 64 frames by default. On Clang's Cortex-M images, GDB's own unwinding can repeat a thread's root frame forever.
- `emb info` prints the header.
- `emb threads --json` gives the list as one JSON line.

The target must be halted. Any GDB server will do: QEMU's gdbstub, OpenOCD, pyOCD.

### On a memory dump

Without a debugger, the reader lays RAM dumps over the ELF:

```sh
python3 tools/descriptor/descriptor.py build/cm4-an386/app.elf --dump 0x20000000:ram.bin [--json]
```

Whatever no dump covers is read from the ELF's loaded sections: the constants, and the initial values of the data. Without any dump, the list shows the image as it was before it ran, which only checks the descriptor itself.

## What checks it

Three tests check the descriptor:

- **`tests/conformance/test_debug_descriptor.c`.** It knows only `<emb/debug.h>`. The test:
  - puts threads in known states: one sleeping, one suspended, one waiting for a mutex the runner holds, and on Cortex-M with an FPU one switched out with a live floating-point context;
  - finds each thread through the descriptor;
  - checks state and wait reason against `emb_thread_state()`, plus priorities, inheritance, the owned mutex, the stack, and that each saved frame lies in its stack.

  It runs on every port that enables the descriptor:
  - the CI's native, Cortex-M and RISC-V presets;
  - the tiny profile when selected: native and micro:bit, both passed by hand.
- **`tools/descriptor/gdb_check.py`.** This one runs the same image under GDB through QEMU's gdbstub (CTest `test_debug_descriptor_gdb`, where a GDB with Python is installed). It lists the threads with `emb threads`, then takes each switched-out thread's backtrace with `emb bt`. A backtrace reaches the thread's own function only when the frame is read right: `sleeper()`, `parked()`, `waiter()`, `fpu_user()`. It passes on:
  - Armv6-M (micro:bit, tiny, no names);
  - Armv7-M (AN385);
  - Armv7E-M with FPU (AN386, AN500);
  - Armv8-M Mainline with PSPLIM (AN505);
  - RV32 (virt).
- **`tools/descriptor/test_descriptor.py`.** It checks the reader on synthetic memory: plain and floating-point Cortex-M frames, the high-water mark, refused magic and version, and an older kernel with fewer values. It also checks the reader's index list against the header's enum.

## Not yet

- **Hardware:** no debugger session on a board yet. On the NUCLEO, the plan is OpenOCD's GDB server with `emb_gdb.py`, which needs nothing new on the target.
- **OpenOCD's own thread list:** `info threads` in GDB through OpenOCD needs an RTOS module in OpenOCD itself, written in C. A module that reads this descriptor would serve every EmbLink version; it is not written. pyOCD, whose RTOS plugins are Python, is the nearer target.
- **AVR and Xtensa frames:** not described, and the descriptor is off by default on AVR, whose constants live in RAM.
- **Partitions, devices, trace rings and monitors:** they have no values until those subsystems exist. The crash record has its own tools ([crash record](crash-record.md)).
