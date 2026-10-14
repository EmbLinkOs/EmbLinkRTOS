# EmbCC storage-layout marker correction

**Evidence date:** 2026-10-15. **Change source:** `3578006bc306a96a8eaf15076668ac9d9c9f78fd`. **EmbCC:** `a3c3ef4c81f9de977b8741ed16072cadca921788`. The [capability review](embcc-capability-review.md) identified a missing RTOS portability macro after upstream's compiler improvements. This correction supplies that macro without an injected header and verifies storage generation on the actual compiler. Kernel/port algorithms and runtime build/link strategy are unchanged.

## Correction and regression

`include/emb/compiler/embcc.h` now defines `EMB_LAYOUT_MARKER`. It writes `->EMB_PROBE name size alignment` into the probe object with `.ascii`; `i` operands require compile-time constants, `%c` emits bare numbers, and `.p2align 2` realigns instructions following the text. The probe is compiled for layout extraction and never linked or executed as firmware. The generator continues to measure configured kernel types rather than guessing their layout.

`tools/storage/test_layout_marker.py` compiles the selected EmbCC portability header, runs the real generator, and independently compiles twelve C assertions against three sample types with different sizes/alignments. The assertions check exact exported size/alignment and sufficient storage size/alignment, including a 16-byte-aligned type. It never executes the embedded marker bytes. The test fails with the original header and passes with the correction.

The CI lint job runs this contract check using GCC as the assembler driver, explicitly selecting the EmbCC header. That protects the macro/generator contract; it is not an actual EmbCC compiler CI leg. The same test also passed locally with the real EmbCC executable. From the repository root:

```sh
python3 -I -m unittest discover -s tools/storage -p 'test_*.py' -v
EMB_STORAGE_TEST_CC=/path/to/embcc \
  python3 -I -m unittest discover -s tools/storage -p 'test_*.py' -v
```

The explicit compiler path must name an installed executable. No compiler is silently substituted when that check fails.

## Actual EmbCC layout and archive checks

Temporary CMake toolchains used the pinned EmbCC, real board/profile configurations, the repository's storage target and unchanged generator. Every row passed generation, independent assertions against `embk_thread_t`/`embk_sem_t`/`embk_mutex_t`, and compilation of the freestanding `emb_kernel` static archive. The checks did not link complete images, execute the port or run EmbCC conformance tests.

| Configuration | Thread bytes / alignment | Semaphore bytes / alignment | Mutex bytes / alignment |
|---|---|---|---|
| Native checked base | 352 / 16 | 64 / 8 | 64 / 8 |
| Native release base | 352 / 16 | 64 / 8 | 64 / 8 |
| Native tiny | 256 / 16 | 48 / 8 | 48 / 8 |
| Native periodic base | 352 / 16 | 64 / 8 | 64 / 8 |
| Cortex-M3, mps2_an385 | 128 / 8 | 36 / 4 | 32 / 4 |
| Cortex-M4F, mps2_an386, hard float | 128 / 8 | 36 / 4 | 32 / 4 |
| RV32, riscv32_virt | 128 / 8 | 36 / 4 | 32 / 4 |
| AVR, arduino_uno, tiny | 35 / 1 | 10 / 1 | 13 / 1 |

The four native generated headers are byte-identical to their GCC counterparts. The marker reports the kernel object's size/alignment; the generated storage union can be larger/more aligned because of its maximal-alignment member. These values are configuration-specific and are not total RAM/footprint figures.

Native GCC was rebuilt and passed all twelve CTest targets. The new single storage contract case passed on GCC and EmbCC with zero skips; the original-header diagnostic intentionally failed. Header formatting, banned-construct and workflow checks passed. All eight compiler/build command groups completed with zero exit codes; unsupported warning options still report their existing diagnostics, and no new warning suppression was added.

The [evidence archive](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/tools/toolchain/evidence/embcc-layout-2026-10-15) retains configuration snapshots, generated headers, assertion/toolchain sources, compiler/source/configuration/header fingerprints, commands and build/test logs. Readable logs strip trailing whitespace; `raw-logs.json` preserves the original text. The temporary toolchains are investigation inputs, not new supported presets or automatic CI qualifications.

## Native build advances to the POSIX boundary

The previously failing Linux-native build now completes the storage probe and generates `storage.h`. It then fails while compiling `arch/native/native_port.c`: the selected EmbCC Linux include/runtime setup has no `pthread.h`. This confirms the marker correction and identifies the next independent integration boundary. The compile-only CMake override isolates these compile checks; no successful Linux-native link or execution is claimed.

The next joint session must choose and validate the native host strategy: compatible POSIX headers/runtime/linking, or an explicit host-compiled port/harness boundary with ABI checks. Do not fabricate pthread declarations or rely on a silently mixed runtime. Target runtime installation, AVR flash/registration wiring and complete compiler/port conformance remain separate tasks. The original capability-review failure records retain their source identities and outcomes.
