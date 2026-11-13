# Logging, tracing and the console

This page shows how to log from an application, how to see the logs, how to record and view the kernel's trace, and how the `emb` tool fits around them. The design is SPEC-015 as [A127 and A128](../specs/AMENDMENTS-M4.md) make it concrete.

## Logging

A module is defined once, in one file, and declared in each other file that logs for it. Each file gives the most detailed level it compiles:

```c
#include <emb/log.h>

EMB_LOG_MODULE_DEFINE(sensor, EMB_LOG_LEVEL_INF);   /* in one file */
/* EMB_LOG_MODULE_DECLARE(sensor, EMB_LOG_LEVEL_DBG);   in the others */

void read_sensor(unsigned id)
{
    int status = sample(id);
    if (status < 0) {
        EMB_LOG_ERR(sensor, "read %u failed: %d", id, status);
    }
    EMB_LOG_DBG(sensor, "raw %x", raw_value(id));      /* compiled out at INF */
    EMB_LOG_HEXDUMP_INF(sensor, frame, len, "rx");     /* bytes, cut at CONFIG_EMB_LOG_HEXDUMP_MAX */
}
```

- A call takes a printf format and at most eight arguments; the compiler checks them against the format as it checks printf's. Strings are copied, up to `CONFIG_EMB_LOG_STR_MAX` bytes.
- A call above its file's level, or above `CONFIG_EMB_LOG_LEVEL_MAX` (information by default), compiles to nothing. `emb_log_level_set(EMB_LOG_MODULE(sensor), EMB_LOG_LEVEL_WRN)` silences a module at run time.
- A call never waits and never formats: it packs its record into a ring in a few microseconds, from a thread or an interrupt. A record that does not fit is dropped and counted, and the next record says how many were lost.
- A log thread at the lowest priority sends the records. On the tiny profile there is no log thread (`CONFIG_EMB_LOG_THREAD`): call `emb_log_flush()` from a thread of your own. A fault handler can call `emb_log_flush()` too, to send what is left.
- Logging costs nothing until an image logs: the ring, the thread and the formatter come with the first call. It is not offered on AVR yet.

### Two backends

**Plain text** is the default. The log thread formats each record on the board and prints a line:

```text
[1.234] E sensor: read 3 failed: -5
```

**Deferred** is the backend when the console is framed (below). The board sends the record as it is, and the host formats it from the image's ELF: the formats and file names never take flash, the image is smaller (3 KB on the logging example), and the console carries about half the bytes. Both print the same lines; the CI checks it on three targets.

## The framed console

Built with `CONFIG_EMB_CONSOLE_FRAMED=y`, everything on the console travels in frames (SPEC-015 §7): the board's text on channel 3, log records on 0, trace on 1, crash records on 2. A terminal shows them as noise; `emb monitor` decodes them:

```sh
scripts/emb build -b nucleo_f446re -S examples/logging --framed
scripts/emb flash -B build/logging-nucleo_f446re
scripts/emb monitor -B build/logging-nucleo_f446re --where
```

```text
[0.000] I app: logging on nucleo_f446re  (main.c:45)
[0.077] W emb_log: 134 records dropped
```

`--where` adds each call's file and line. The monitor refuses a stream from another build of the image: decode with the image's own ELF. Text that is not a frame, such as a boot ROM's message, is shown as it is.

## The trace

With `CONFIG_EMB_TRACE=y`, `emb_trace_start()` records the kernel's events (switches, waits, wakes, interrupts, objects) into a RAM ring, the newest overwriting the oldest. `emb_trace_dump()` sends the ring on the framed console, the threads' names first. `CONFIG_EMB_TRACE_STREAM` sends new events every period instead.

```c
emb_trace_start();
run_the_part_to_look_at();
emb_trace_stop();
emb_trace_dump();
```

```sh
scripts/emb build -b nucleo_f446re -S examples/tracing      # sets the trace and the framed console
scripts/emb flash -B build/tracing-nucleo_f446re
scripts/emb trace dump -B build/tracing-nucleo_f446re --until "tracing: done" \
    --ctf build/trace --perfetto build/trace.json
babeltrace2 build/trace                                      # or open it in Trace Compass
```

Open `trace.json` in [Perfetto](https://ui.perfetto.dev): a track per thread with its running slices, one for interrupts, and the other events as marks. The times come from the cycle counter: 5.6 ns steps at 180 MHz.

## The emb tool

`scripts/emb` (put `scripts/` on your `PATH`) knows a build directory's board, image and console:

| Command | Does |
|---|---|
| `emb build -b <board> -S <app> [--clang] [--framed] [--set SYM=val]` | configure and build, with the board's toolchain |
| `emb flash -B <build>` | program the image with the board's probe: OpenOCD, picotool, avrdude or embflash |
| `emb monitor -B <build> [--where] [--until TEXT]` | the console, frames decoded |
| `emb hil -B <build> [-R test]` | the test suite on the board, for a build made with `--hil --port` |
| `emb trace dump -B <build> --ctf DIR --perfetto FILE` | the trace an image dumps, as CTF and Perfetto's JSON |

`emb build -n` prints the commands without running them.
