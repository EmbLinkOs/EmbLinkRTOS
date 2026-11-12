# The entropy class on hardware: NUCLEO-F446RE and Pico 2 W (2026-11-12)

This report covers the entropy class of SPEC-017 §11 (SEC-012, A124).

On the Pico 2 W, the RP2350's TRNG passed every health test the block has. Its output, conditioned with SHA-256 at 2:1, passed the board's own sanity checks over 1 MiB. A 64 KiB sample of that output, printed on the console, then passed the host's checks. The source's raw blocks were also measured: their min-entropy is well above the 4 bits a byte that conditioning at 2:1 needs.

The NUCLEO-F446RE's STM32F446 has no RNG. The class reported that absence as `EMB_ENOTSUP`, and nothing stood in for a source.

The whole conformance suite ran on each board with GCC and with Clang, and every image passed. The native board's source is the host's `getentropy()`, and it runs the same board test in CI.

**Commit:** `2a7e2ed`, from a clean tree (branch `feat/m4.2-driver-classes`, PR #140). The class and the TRNG driver are `e23a4f0`. `2a7e2ed` adds the conditioning and the raw read (see "Found on the way").

**Bench:** the same wires as the UART, SPI and I2C reports. The entropy class needs no wires of its own. The sources:
- **Pico 2 W:** the RP2350's TRNG (datasheet §12.12), a ring oscillator sampled into 192-bit blocks. It is sampled every 200 cycles of its clock, a value the board's description sets. Every health test the block has is on: the von Neumann corrector, the continuous test and the autocorrelation test. A block's end, or a test's failure, raises interrupt 39, which wakes the read.
- **NUCLEO-F446RE:** none. The STM32F446 has no RNG peripheral, so the generated `emb_board_entropy` is `NULL`.

`ctest` ran on `cm4-nucleo-hil` and `cm33-pico2w-hil`, checked, with the UART, SPI and I2C loops as before. On the Pico, `test_entropy_board` prints its sample (`EMB_ENTROPY_DUMP`) and is given 300 s (`EMB_HIL_TIMEOUT_test_entropy_board`). Each run's records are under `hil-runs/`:

| Board | Compiler | Passed | Cases recorded |
|---|---|---|---|
| NUCLEO-F446RE | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 47 of 47 | 280 |
| NUCLEO-F446RE | Homebrew Clang 23.1.5 with LLD 23.1.5 | 47 of 47 | 280 |
| Pico 2 W | Arm GNU Toolchain 14.2.Rel1 (GCC 14.2.1) | 49 of 49 | 284 |
| Pico 2 W | Homebrew Clang 23.1.5 with LLD 23.1.5 | 49 of 49 | 284 |

## What the runs show

**The conditioned output** (`test_entropy_board`, 1 MiB through `emb_drv_entropy_read()`, judged on the board):

| What | Pico 2 W, GCC | Pico 2 W, Clang | Bound |
|---|---|---|---|
| 1 MiB took | 98.9 s | 98.0 s | — |
| Blocks that failed a health test | 17 | 16 | at most 1024 |
| Byte frequencies, chi-square (255 degrees) | 249.4 | 275.8 | 120 to 400 |
| Ones, of 8,388,608 bits | 4,196,085 | 4,192,266 | half, ± 8000 |
| Bit transitions | 4,194,348 | 4,193,171 | half, ± 8000 |
| Longest repetition of a byte | 3 | 3 | at most 5 |

The rate is some 10.6 KB/s conditioned, or 21 KB/s of raw blocks. That is enough for seeding a generator, which is the class's purpose.

**The host's checks** (`tools/entropy/check.py`) ran on each run's 64 KiB sample, read back from the archived log. The sample's SHA-256 matched the one the board printed.

| Check | Pico 2 W, GCC | Pico 2 W, Clang |
|---|---|---|
| Monobit | z = 0.78 | z = 1.34 |
| Byte frequencies, chi-square (255 degrees) | 233.3 | 246.4 |
| Runs of equal bits | z = 0.56 | z = 1.11 |
| Serial correlation of the bytes | −0.00138, z = 0.35 | +0.00929, z = 2.38 |
| zlib's compression | 1.0004 of the input | 1.0004 of the input |

All of them pass. One z of 2.38 among a dozen checks is what chance gives.

**The raw source** (`emb_drv_entropy_read_raw()`, 256 KiB). It was measured, not judged by the tests above:

| What | Pico 2 W, GCC | Pico 2 W, Clang |
|---|---|---|
| Most common byte value | 1304 times | 1225 times |
| Min-entropy, SP 800-90B §6.3.1's upper bound | 7.55 bits a byte | 7.64 bits a byte |
| Byte frequencies, chi-square (255 degrees) | 1231.6 | 1404.1 |
| Bit transitions, of 2,097,151 | 1,067,308 | 1,023,963 |

The board asserts the 4 bits a byte that conditioning at 2:1 needs, and both runs clear it by far. The raw chi-squares and transition counts show why the class conditions its source: the raw bytes are biased, and their bits are correlated. Which way the transitions lean varied between the two runs.

**The class's core** (`test_entropy`) ran on both boards as on the host:
- the conditioning function against FIPS 180-4's examples;
- reads assembled from a driver's blocks, raw and conditioned;
- a block that fails its health test delivering none of its bytes;
- a timeout keeping what was read;
- misuse faulting.

On the NUCLEO, `entropy_board_source_or_its_absence` saw `EMB_ENOTSUP` with no device in its place. On the Pico, it read 64 conditioned bytes from the TRNG.

## Found on the way

- **The raw TRNG is not uniform.** The first runs (`e23a4f0`) handed out the TRNG's blocks as they came. Over 1 MiB, the chi-square of the byte frequencies came out at 6381.9 and 2630.0, against a mean of 255. The bit transitions were also short of half. That is what SP 800-90B expects of a raw noise source, and why it asks for conditioning. The class now hashes every 64 raw bytes into 32 with SHA-256 (`2a7e2ed`). The raw read stays available for measuring the source. The source's own health tests run on the raw blocks, before conditioning.
- **The sampling interval.** The board's `sample_count` sets how many TRNG clock cycles pass between two samples of the ring oscillator. Each interval was swept over 64 KiB with every health test on:

  | Cycles | Result |
  |---|---|
  | 400 | 64 KiB in 5742 ms, 1 health failure |
  | 200 | 64 KiB in 2678 ms, none |
  | 100 | 64 KiB in 1339 ms, none |
  | 50 | 264 bytes before the limit of 400 health failures |
  | 25 | 24 bytes before the limit |
  | 17 | 552 bytes before the limit |

  At 50 cycles or fewer, nearly every block fails. 100 passes, but it sits next to that edge, so the board uses 200: twice the rate of 400, with the margin. pico-sdk's `pico_rand` sidesteps the question by bypassing the health tests. This driver keeps them on.
- **An autocorrelation failure clears only with a reset.** Restarting the source clears the von Neumann and continuous tests' failures. The autocorrelation test's failure stays latched until the block is reset, so the driver resets the TRNG and sets it up again after one.
- **A bench hiccup.** On its first try, the Pico GCC run found "no board in BOOTSEL" for most of its images. It was rerun unchanged, and the run above is that rerun.
