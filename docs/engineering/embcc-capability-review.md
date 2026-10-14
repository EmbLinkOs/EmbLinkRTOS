# EmbCC: Ten legacy limitations reviewed

**Reviewed:** 2026-10-14. **EmbCC:** `a3c3ef4c81f9de977b8741ed16072cadca921788`. **EmbLinkRTOS:** `2e59c2a37b36c90e5f15892fb258c8a2e21be306`. The ten gaps recorded against EmbCC `387eb23` are now implemented upstream within the scopes below. This review updates the compiler fact base; it does not qualify every RTOS compiler/port combination or implement the missing integration.

The [toolchain profile](../architecture/09-embcc-toolchain-profile.md) records current design consequences. EmbCC PRs [#107](https://github.com/EmbLinkOs/EmbCC/pull/107), [#108](https://github.com/EmbLinkOs/EmbCC/pull/108), [#113](https://github.com/EmbLinkOs/EmbCC/pull/113) and [#114](https://github.com/EmbLinkOs/EmbCC/pull/114) cover the original ten items; later PRs [#116](https://github.com/EmbLinkOs/EmbCC/pull/116) and [#119](https://github.com/EmbLinkOs/EmbCC/pull/119) extend the toolchain and report RTOS integration experiments. They were observed merged. The scoped local checks below ran on the pinned main source, including the later loop-counter correction merged in PR #121.

## Closure matrix

This page preserves the 2026-10-14 investigation and its recorded failures. The subsequent [layout-marker correction](embcc-layout-marker.md), dated 2026-10-15, supplies the missing macro and verifies storage/kernel-archive compilation on eight configurations. The full Linux-native attempt now stops later at unavailable `pthread.h`; the original evidence below retains its baseline and outcomes.

“Implemented upstream” means the current manuals, implementation and regression fixtures support the named capability. Local check outcomes state what was executed here; object inspection and simulator runs are not hardware qualification. The [evidence archive](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/tools/toolchain/evidence/embcc-2026-10-14) preserves commands, outputs, identities and probe inputs.

| Original item | Current upstream capability | Local check and remaining scope |
|---|---|---|
| 1. Armv8-M `msplim`/`psplim` | Implemented; `_ns` aliases also documented. See [inline assembly][asm] and `tests/golden/thumbv8m-splim.sh`. | Mainline register reads/writes compile; Armv7-M refuses them. Stack-overflow hardware/QEMU behavior was not rerun here. |
| 2. AVR GNU linker scripts | Implemented by `embld -T`, including AVR address-space/layout checks. See [linker][ld] and `tests/golden/avr-ldscript.sh`. | Upstream fixture links with `.data` at `0x800100` and prints its expected values under EmbSim. RTOS-generated linker inputs remain planned. |
| 3. AVR program-memory qualifier | `__flash` and avr-libc-compatible `progmem` support are implemented. Plain generic `const` is still data-space storage. See [embedded programming][embedded] and `tests/golden/avr-flash.sh`. | Fixture objects have `.progmem.data`; EmbSim prints the expected table/string values. Current RTOS EmbCC flash macros still fall back to generic `const`/direct reads and need integration. |
| 4. AVR DWARF | DWARF 4 function/global/line information is implemented, with four-byte debug addresses and AVR frame conventions. See [targets][targets] and `tests/golden/embdbg-remote.sh`. | `readelf` finds the function/global; EmbDBG reads functions and source lines. Live debugging was not executed. No `.debug_frame`; AVR EmbDBG backtrace stops at frame 0. `-g` can increase code size substantially. |
| 5. EmbDBG AVR register layout | Implemented: r0–r31, SREG, SP and a byte-addressed PC. See [debugger][dbg] and its remote golden. | Executed the actual `rsp_regs_for("avr")` host function and checked offsets/widths. No remote target/probe session was attached. |
| 6. Small RV32/AVR atomic RMW | RV32 byte/halfword operations use aligned-word atomics; AVR operations use saved/restored interrupt masking. See [targets][targets], [embedded programming][embedded] and the RISC-V/AVR atomic goldens. | Byte/halfword fetch-add returns and stored values match under EmbSim on both targets. This is no interrupt-race, DMA, SMP or arbitrary memory-order proof. |
| 7. CMSE | `-mcmse`, secure entry/non-secure call support, address checks and gateway/import-library linking are implemented for Armv8-M Mainline and Baseline. See [targets][targets] and `tests/golden/thumbv8m-cmse.sh`. | Entry symbols compile on both profiles; the unsupported FPU combination is refused. Secure builds require soft float; stack-passed crossings and some incremental-link/ACLE features remain unsupported. No local security-crossing execution. |
| 8. Cortex-M23 | `thumbv8m.base-none-eabi` and `-mcpu=cortex-m23` are implemented. See [targets][targets] and the `thumbv8mbase-*` goldens. | The compiled object's attributes identify Armv8-M Baseline. Upstream's documented QEMU execution uses an M33 with a Baseline instruction scan, not an actual M23. No M23 board run here. |
| 9. SVD clusters, arrays and structured output | Nested clusters, arrays and `derivedFrom` plus `embsvd --json` are implemented. See [SVD tool][svd] and `tests/golden/svd-clusters.sh`. | JSON addresses match the independent upstream SVD arithmetic reference and compiled header offsets/static layout assertions. This JSON is not yet the RTOS SoC/board schema or an implemented `--emit-emb-yaml` adapter. |
| 10. C++ on 32-bit embedded targets | Cortex-M/ARM and RV32 C++ code generation is implemented with `-fno-exceptions`; runtime builds and regression suites exist upstream. See [C++][cxx] and `tests/golden/cxx-embedded.sh`. | A C++17 template wrapper compiles for Cortex-M3 and RV32; AVR correctly refuses it. Full RTOS wrapper/runtime tests are unrun. AVR C++ and embedded exception unwinding remain outside this closure. |

All ten focused checks passed at their stated scope. These are investigation checks, not ten full upstream test-suite executions. The finite firmware runs used two million instructions and a 60-second per-command wall-time limit. No Clang comparison, QEMU suite, physical board or remote-debug session was executed locally for this review.

## Evidence of RTOS builds upstream

Merged [EmbCC PR #119](https://github.com/EmbLinkOs/EmbCC/pull/119) reports macOS native 10/10, Cortex-M4/QEMU 10/10, RV32/QEMU 9/9 and Cortex-M3/QEMU 9/9 RTOS test targets. Its description explicitly supplies `EMB_LAYOUT_MARKER` through `-include`, supplies archiver settings on macOS, uses a scratch toolchain for M3 and leaves AVR RTOS execution unrun at that stage. Those counts refer to the author's tested snapshot, not the current RTOS's twelve native CTest targets.

This is upstream-reported execution evidence. The PR text does not preserve a complete per-build RTOS commit/configuration/binary record in this repository, and the source review does not turn those experiments into a current RTOS CI leg. The current `ci.yml` still uses GCC/Clang and cross-GCC.

## Current integration findings

The local tools built with GCC 14.2.0 using a POSIX feature macro and warnings enabled without `-Werror`. Default bootstrap failed on the `readlink` declaration; subsequent strict attempts exposed `format-truncation` and `empty-body` diagnostics. No EmbCC source patch was applied, and this review does not claim a warning-clean GCC bootstrap. These host-build observations are separate from closure of the ten embedded capabilities.

A normal local Linux RTOS configure could not link CMake's executable probe because EmbCC's Linux `crt1.o` was not built/installed. A supported compile-only `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` override let configure complete; the build then stopped at the storage probe because `include/emb/compiler/embcc.h` does not define `EMB_LAYOUT_MARKER`. The failure is archived and was not bypassed with a forced header. No complete local EmbCC RTOS binary or conformance suite passed in this review.

The next integration session should:

1. Supply the compiler-port layout marker and check generated storage size/alignment, including the fixed-width handle ABI. Keep source changes in the joint coding session.
2. Select and build/install the intended target runtime; choose the native host-link/threading strategy explicitly. Confirm CMake compiler/archiver identification without depending on an unrecorded injected header.
3. Wire EmbCC's AVR program-memory declarations/accessors and named registration sections into the existing portability boundary. Do not assume upstream `__flash` automatically changes generic RTOS `const`.
4. Run native checked/release/tiny/periodic, supported embedded conformance and header/C++ checks on pinned compiler/RTOS sources; archive skips, configuration, binaries and results before adding or claiming an EmbCC CI leg.
5. Preserve remaining kernel policies: no compiler TLS, no computed-goto dispatch, no LTO/PIC/stack-protector assumption, fixed-width ABI fields and target-bounded atomic protocols. Eight-byte helper atomics on 32-bit targets mask privileged local interrupts by default; they do not establish SMP/DMA or unprivileged atomicity.

HW-006's two-output generator target is retained for compatibility; AVR scripts are now available, so removing the legacy option-set requirement would be a separate reviewed design amendment. EmbFlash now exists upstream as a GDB-server flash client, but RTOS flashing/board support and its proposed `emb flash` facade still need their own integration evidence.

[asm]: https://github.com/EmbLinkOs/EmbCC/blob/a3c3ef4c81f9de977b8741ed16072cadca921788/docs/manual/inline-asm.md
[ld]: https://github.com/EmbLinkOs/EmbCC/blob/a3c3ef4c81f9de977b8741ed16072cadca921788/docs/manual/tools/embld.md
[embedded]: https://github.com/EmbLinkOs/EmbCC/blob/a3c3ef4c81f9de977b8741ed16072cadca921788/docs/manual/embedded.md
[targets]: https://github.com/EmbLinkOs/EmbCC/blob/a3c3ef4c81f9de977b8741ed16072cadca921788/docs/manual/targets.md
[dbg]: https://github.com/EmbLinkOs/EmbCC/blob/a3c3ef4c81f9de977b8741ed16072cadca921788/docs/manual/tools/embdbg.md
[svd]: https://github.com/EmbLinkOs/EmbCC/blob/a3c3ef4c81f9de977b8741ed16072cadca921788/docs/manual/tools/embsvd.md
[cxx]: https://github.com/EmbLinkOs/EmbCC/blob/a3c3ef4c81f9de977b8741ed16072cadca921788/docs/manual/cxx.md
