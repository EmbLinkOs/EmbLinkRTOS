# Logging, the trace, the framed console and the two-way relay on hardware: NUCLEO-F446RE and Pico 2 W (2026-11-13)

M4.3's pieces on both boards: logging with both backends (A127), the trace ring (A128), the framed console, `emb monitor`, and the NUCLEO's relay working both ways. The whole conformance suite ran on each board with GCC and with Clang, and every image passed, `test_frame` and `test_log` among them. Each board then ran the framed console's soak for 30 minutes: no frame was damaged, and every record that did not arrive was one a dropped record counted.

**Commit:** `34dde4f3` for the suites, from a clean tree (branch `feat/transport-frame`, PR #139). The soaks ran `a5b6b75e` (NUCLEO-F446RE) and `1b58e07b` (Pico 2 W), the examples `9af19156` (NUCLEO-F446RE) and `c8d07630` (Pico 2 W); the subsystems' code is the same in all of them but for `34dde4f3`'s renames and fixes for the static analysis.

**Bench:** the wires of the UART, SPI and I2C reports, and one more for the relay's way to the Pico 2 W: the NUCLEO's CN10 pin 21 (PA9) to the Pico's pin 2 (GPIO1). The NUCLEO-F446RE's console is its ST-LINK's virtual COM port; the Pico 2 W's is the NUCLEO running `tools/hil/nucleo-uart-relay`, both ways at 115200 baud. The NUCLEO-F446RE runs at 180 MHz, the Pico 2 W at 150 MHz.

## The suites

`ctest` ran on `cm4-nucleo-hil` and `cm33-pico2w-hil`, checked, with the UART, SPI and I2C loops as before. Each run's records are under `hil-runs/`:

| Board | Compiler | Passed | Cases recorded |
|---|---|---|---|
| NUCLEO-F446RE | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 49 of 49 | 296 |
| NUCLEO-F446RE | Homebrew Clang 23.1.5 with LLD 23.1.5 | 49 of 49 | 296 |
| Pico 2 W | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 51 of 51 | 300 |
| Pico 2 W | Homebrew Clang 23.1.5 with LLD 23.1.5 | 51 of 51 | 300 |

`test_frame` (9 cases) checks the codec against the host tool's vectors, its decoder's resynchronization and damage, and `emb_frame_write()` streaming exactly the bytes `emb_frame_encode()` writes. `test_log` (7 cases) checks the record's layout, the plain-text formats, the levels, a full ring dropping and counting, an interrupt's record held back by an uncommitted one, and three threads at three priorities with an interrupt logging at once while the test drains the ring: every producer's records came whole and in order, and those kept plus those counted dropped were all that were written (2,828 checks with GCC, 2,855 with Clang, on each board). `test_trace_ring` needs `CONFIG_EMB_TRACE`, which the boards' suites leave off: it runs natively, under ASan and TSan, in CI.

## The framed console's soak

`tools/hil/log-soak` logs a numbered record every 10 ms, a burst of 100 every second, more than the 1 KiB ring keeps while the log thread waits for the console, and both rings' counters every 5 s, while the trace streams on channel 1 every 500 ms. `tools/hil/log_soak.py` read each board's console for 30 minutes:

| | NUCLEO-F446RE | Pico 2 W, through the relay |
|---|---|---|
| Frames | 149,411 | 145,624 |
| Damaged or too long | 0 | 0 |
| Log records decoded | 295,211 | 270,615 |
| Numbered records | 180,011 | 154,761 |
| Dropped, counted in band | 67,320 | 91,749 |
| Missing and not counted | 0 | 0 |
| Trace packets (events) | 23,400 (262,800) | 25,953 (277,999) |

The trace's stream fills most of the line, so the log thread often waits for the console and the bursts drop records: that is the soak's point, and the accounting held through every one of them. The trace ring overwrites most events between two sends (the Pico 2 W's counters: 6,299,436 recorded, 6,020,401 overwritten, 277,371 sent), and each packet's first event number shows the host what it missed. The records are in `hil-runs/2026-11-13-nucleo_f446re-log-soak` and `hil-runs/2026-11-13-pico2_w-log-soak`, each with the counts, the first 256 KiB of the console as captured, and the whole capture's size and SHA-256 (10 MB a soak is too much for the repository; the captures are kept with the maintainer).

The NUCLEO's 30 minutes ran the soak before a fix: its counters thread logged on the same tick as the bursts, when the ring was full, so its two records were dropped every time, and counted. They log half a second later since `1b58e07b`; the Pico 2 W's soak shows them, and so does a 5-minute soak on the NUCLEO at `34dde4f3` (`hil-runs/2026-11-13-nucleo_f446re-log-soak-counters`): 24,841 frames, none damaged, 29,988 numbered records, 11,100 dropped and counted, none missing, and the counters every 5 s (the last: 48,547 records committed, 10,915 dropped, 48,546 sent).

## The examples

- **Logging** (`examples/logging`), plain and framed on each board: `tools/logdec/backends_agree.py` found the same lines, and each build's burst kept its first records in order and counted the rest. The framed console carried 927 bytes on the NUCLEO-F446RE and 931 on the Pico 2 W, against 1748 and 1742 as plain text, and the framed image is 3 KB smaller: the formats stay in the ELF.
- **`emb`**: `emb build --framed`, `emb flash` and `emb monitor --where` on the NUCLEO-F446RE showed the records decoded with their call sites.
- **Tracing** (`examples/tracing`): 256 events over 16 ms on each board, the threads named, times at the cycle counter's resolution (3.7 µs between the first two events on the NUCLEO-F446RE). `tools/trace/trace.py` wrote the CTF trace and Perfetto's JSON from them.
- **The relay, both ways** (`examples/console_echo` on the Pico 2 W): "hello" and "through the relay" came back in capitals, and the 95 printable characters came back whole, in two lines of the example's 64-byte buffer. The bench commands on channel 15 answered: `ping` and `wake 50` with `ok`, an unknown one with `?`.

## Found on the way

- **A metadata at the null pointer.** The deferred backend's section is laid at address 0, so its first call site's metadata had the address 0, and `emb_log_write()` took it for a missing argument and returned. The QEMU AN386 run lost exactly that call's record. The generated linker scripts now leave the section's first 8 bytes empty (`7011cbd0`).
- **The dropped record's length.** Its header was sized for a 4-byte identifier where the record wrote 2: two stale bytes followed it. Found on native, fixed before the first commit.
- **One thread per priority on tiny.** The table scheduler's priorities hold a thread each, so the log thread took the test runner's priority on the micro:bit; there is no log thread on tiny (`9af19156`, A127).
- **AVR.** The plain-text formatter and its stack did not fit the Uno's 2 KiB beside the test framework: logging is not offered on AVR in M4.3. Its call sites are already read from program memory there.
- **`%u` and `uint32_t`.** On Arm and RISC-V newlib `uint32_t` is `unsigned long`: the format check (`EMB_PRINTF_FORMAT`) caught a test passing one to `%u`, which compiled on the Mac's native build.
- **A test buffer the Uno could not afford.** `test_frame`'s streaming case first kept a second frame buffer; under EmbSIM the Uno ran into its stack. The sink now compares each byte as it comes (`45e7f515`).
