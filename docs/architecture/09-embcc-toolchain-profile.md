# 09 - EmbCC Toolchain Profile

**Status:** Compiler capability review refreshed against EmbLinkOs/EmbCC at `a3c3ef4c81f9de977b8741ed16072cadca921788`, 2026-10-14, superseding the `387eb23` snapshot. The [ten-item review](../engineering/embcc-capability-review.md) names current manuals, implementation/regression evidence and scoped local checks, and separates them from upstream-reported RTOS experiments. The *consequence* lines remain RTOS design rules unless explicitly amended; a compiler improvement does not silently relax them. Current EmbCC RTOS integration is incomplete and is not a configured CI leg.

---

## 1. What EmbCC is

EmbCC is a C and C++ compiler written in C99 with its own preprocessor, per-target assemblers, linker (`embld`), archiver (`embar`), SVD tool (`embsvd`), debugger (`embdbg`) and additional firmware tools. It targets embedded systems and hosted/bare-metal operating-system programs; one compiler handles each supported target. Runtime, C-library and supported C++ runtime builds exist upstream, including a separate AVR library. They must be built/installed for the selected target; their availability does not introduce a libc dependency into the RTOS kernel. (`README.md`, `docs/manual/targets.md`, `docs/manual/getting-started.md`)

## 2. Targets that matter to EmbLinkRTOS

| Family | Triples | Notes for the RTOS |
|---|---|---|
| Cortex-M, Armv6-M | `thumbv6m-none-eabi` (Cortex-M0, M0+, M1) | Thumb-1 lowering; divide, 64-bit multiply, and atomics are `librt.a` calls; the atomic helpers mask with PRIMASK, which does nothing in unprivileged Thread mode, so an isolated profile on Armv6-M would need its own atomics |
| Cortex-M, Armv7-M | `thumbv7m-none-eabi` (M3) | Reference port target |
| Cortex-M, Armv7E-M | `thumbv7em-none-eabi`, `thumbv7em-none-eabihf` (M4, M4F, M7 with `-mcpu=cortex-m7` for FPv5-D16) | Hard-float objects do not link with soft-float ones; one `librt.a` per float ABI |
| Cortex-M, Armv8-M Mainline/Baseline | `thumbv8m.main-none-eabi[hf]` (M33), `thumbv8m.base-none-eabi` (M23) | Stack-limit assembly and M23 selection are implemented. Secure-side `-mcmse` supports both profiles with soft float; the FPU combination is refused. Compiler support does not establish an RTOS secure port or stack-overflow hardware evidence |
| RISC-V | `riscv32-unknown-elf` (default RV32IMAC, ilp32), `riscv64-unknown-elf` (default RV64IMAC, lp64) | ISA/ABI options include F/D and hard-float runtimes upstream; the current RTOS port uses its own RV32IMAC scope. Consult `docs/manual/targets.md` for accepted `-march`/`-mabi` combinations |
| AVR | `avr`, with `-mmcu=atmega328p`, `atmega328`, `atmega168p`, `atmega168` upstream | `double` is 32-bit. Generic `const` still costs SRAM; `__flash` and avr-libc-compatible `progmem` are implemented. Current RTOS EmbCC macros still use generic fallback reads, so flash placement needs explicit integration |
| MIPS32 | `mipsel-none-elf` (MIPS32r2, PIC32 class) | A candidate fifth architecture port; not planned |
| Hosts | `x86_64-linux-gnu`, `x86_64-apple-darwin`, `aarch64-*`, `x86_64-windows-gnu` (incomplete) | C++ supported on x86-64 and AArch64 only |

The RTOS reference targets above are little-endian; EmbCC also has other, including big-endian, targets outside this scope. C++ is implemented on Cortex-M/32-bit ARM and RV32 with `-fno-exceptions`; AVR C++ remains refused. Use current `docs/manual/targets.md` and `docs/manual/cxx.md`, plus regression fixtures, rather than older contradictory C-only rows in `docs/internals/status.md`.

**Consequence.** EmbCC supplies compiler targets for the reference architecture families. Each RTOS compiler/port combination still needs its own storage, linking, ABI and executed conformance evidence. GCC/Clang remain the active matrix; a target's existence does not establish an EmbCC CI job or qualify a port.

## 3. Language level

- One dialect: C17 plus GNU extensions plus part of C23; `-std=` is accepted and ignored, `__STDC_VERSION__` is `201710L`. `_Static_assert`, `_Generic`, `_Noreturn` supported; `_Alignas` partial; `_Atomic` for integer and pointer types only; `<stdatomic.h>` and `__atomic_*`/`__sync_*` builtins provided. (`docs/manual/c-language.md`)
- **`_Thread_local` and `__thread` compile to one shared instance on every embedded target**, without an error. (`docs/internals/status.md` §Thread-local storage)
- `__STDC_HOSTED__` is 1 even with `-ffreestanding`. Empty parameter list `()` always means no parameters. A non-void function that can reach its closing brace is an error. Block-scope tags and typedefs stay visible to the end of the translation unit. (`docs/manual/c-language.md`)

**Consequences.**
- The C11 minimum of ADR-023 is enforced by the GCC and Clang legs of the matrix with `-std=c11 -pedantic`; EmbCC cannot enforce it.
- The kernel and every port **never use `_Thread_local` or `__thread`**. Per-thread data is kernel TLS slots only (KRN-THR-013, KRN-THR-014). A static-analysis rule bans the keywords tree-wide.
- The kernel does not test `__STDC_HOSTED__`.

## 4. Attributes and builtins

Supported where the RTOS needs them: `naked` (Cortex-M, RISC-V, MIPS32, AVR), `interrupt` (Cortex-M: accepted, no code change; AVR: implemented; RISC-V: refused), `signal` (AVR), `section` (static storage), `used`, `weak`, `alias` (functions), `aligned` (including typedefs and supported over-aligned locals), `packed`, `constructor` (including priorities), `noreturn`, `always_inline`, `pcs`. Provided builtins include `__builtin_memcpy`/`memset`/`memmove`/`memcmp`, `__builtin_trap`, `__builtin_expect`, the atomics, `__builtin_sqrt`, `__builtin_fabs`. (`docs/manual/extensions.md`)

Some extensions remain refused, including `cleanup`, `weakref` and `ifunc`; consult current `docs/manual/extensions.md` for exact attribute/builtin diagnostics. Automatic storage cannot have a linker section. The old refusal of constructor priorities and typedef alignment is no longer a compiler limitation.

**Consequences.**
- Device and subsystem initialization order remains a generated table (DRV-001), never constructor priorities, although the compiler now implements them.
- No scope-guard macros built on `cleanup`.
- Alignment remains on object declarations and struct definitions, never typedefs, as the RTOS portability policy. EmbCC now supports typedef alignment; the storage-member alignment correction of M1 remains necessary independently of that earlier compiler restriction.
- Register-file-wide bit operations use the `emb_arch_` layer (`clz`, `ctz`, `popcount`) with compiler-portability fallbacks; the exact `__builtin_clz` family availability is verified by a probe at configure time.

## 5. Inline assembly and naked functions

- Inline asm is assembled by EmbCC's own assembler per target with a **fixed instruction vocabulary** (documented per target). Labels are not accepted inside inline asm; branch targets are `.+N`. Only `r`/`m`/`i`-style constraints; callee-saved registers may not be named or clobbered in a template (r4-r11 on Cortex-M, s0-s11 on RISC-V, r2-r17 and Y on AVR). An asm output wider than a register is refused.
- Cortex-M vocabulary includes `mrs`/`msr` on the kernel registers and Armv8-M stack limits; barriers, masking, sleep, context-save and FPU sequences; and now `ldrd`/`strd`, `clrex` and byte/halfword exclusives on applicable cores. Core-specific restrictions still apply; use `docs/manual/inline-asm.md` and the encoding/RTOS goldens before relying on another instruction.
- RISC-V vocabulary covers CSR access by name or number, `mret`/`sret`/`wfi`/`ecall`/`fence`, loads, stores, ALU, branches. Missing in inline asm: `lr`/`sc`/`amo*` (the compiler emits them for C atomics), `la`/`call`/`tail` pseudo-instructions, floating point. `.S` files use the same assembler and do accept `call`.
- AVR vocabulary is the whole AVR5 set, including `in`/`out`, `cli`/`sei`, `push`/`pop`, `reti`, `sleep`, `wdr`, `lpm`.
- A `naked` function body is assembled as a file-scope block: it may hold labels, literal pools, and zero-argument calls into C; it may not hold other statements or asm outputs. FreeRTOS's Cortex-M3, M4F, M7, M33 non-secure, and RISC-V ports compile unmodified and run on QEMU. (`docs/manual/inline-asm.md`, `docs/manual/embedded.md` §An RTOS kernel)

**Consequences.**
- The architecture-port contract (02 §2) is implementable on all three EmbCC embedded families in C plus inline asm, with the context switch and first-thread launch as `naked` functions and the RISC-V trap entry as a `.S` file.
- Every critical-section primitive is `static inline __attribute__((always_inline))` with a `"memory"` clobber (the inliner otherwise declines functions containing inline asm).
- Kernel atomics are C11 atomics through the portability layer, never hand-written `lr`/`sc` or `ldrex`/`strex` templates, so one source serves EmbCC, GCC, and Clang.
- No labels in inline asm: any multi-block assembly sequence lives in a `.S` file or a `naked` function.

## 6. Atomics and width limits

| Target | Atomic load/store | Atomic read-modify-write |
|---|---|---|
| Cortex-M Armv7-M, Armv8-M | 1 to 4 bytes | 1 to 4 bytes inline (`ldrex`/`strex`) |
| Cortex-M Armv6-M | 1 to 4 bytes | 1 to 4 bytes via `librt.a` (PRIMASK masking) |
| RV32 | 1 to 4 bytes | 1, 2 and 4 bytes; subword RMW operates on the aligned containing word |
| RV64 | 1 to 8 bytes | 1, 2, 4 and 8 bytes |
| AVR | 1 to 8 bytes, masking as needed | 1 to 8 bytes inline with saved/restored SREG masking; not lock-free |

Eight-byte atomics on 32-bit targets now call sized `__atomic_*_8` runtime helpers. The default weak helpers mask privileged local interrupts: they are not a multicore/DMA or unprivileged atomicity solution. AVR's wide operations are inline masking sequences. Computed `goto` is now implemented upstream; the kernel prohibition remains policy. `__int128` still does not exist on 32-bit targets. (`docs/manual/targets.md`, `docs/manual/embedded.md`, `docs/manual/extensions.md`)

**Consequences.**
- 64-bit kernel time remains read/written under a critical section or a sequence lock, never as an atomic 64-bit object (KRN-TIM-016); helper availability does not change this protocol.
- Kernel atomic state is word-sized. Flags packed into bytes are manipulated as words or under a critical section.
- The tiny profile's atomics are interrupt masking by definition; the portability layer makes that explicit rather than relying on compiler fallbacks.
- No computed-goto dispatch anywhere in the kernel.

## 7. Linking, sections, and images

- `embld` links ARM, RV32, RV64, MIPS32, AVR, and x86-64 objects; AArch64 needs an external linker. Static links only, no PLT, `ET_EXEC`, W^X segments.
- Without a script: `-Ttext`, `-Tdata`, `-Tstack` (RISC-V entry stub), `--rom-limit`, `-e`, `--gc-sections`, `-Map`, `--print-memory-usage`; `.vectors`/`.isr_vector` placed first; orphan sections bracketed by `__start_NAME`/`__stop_NAME` or `__NAME_start`/`__NAME_end`.
- **GNU ld linker scripts (`-T`) now include AVR support.** The supported subset includes `MEMORY`, `SECTIONS`, `KEEP`, `PROVIDE`, `ALIGN`, `AT>`, `(NOLOAD)`, `/DISCARD/`, `ASSERT`, `INCLUDE`, `REGION_ALIAS`, `EXTERN`, sorting, fill and expression functions. AVR scripts must respect the separate program/data address spaces. `PHDRS`, `OVERLAY` and `INSERT` remain refused; check the current linker manual for the exact subset.
- `-ffunction-sections`, `-fdata-sections`, `--gc-sections`, `-fstack-usage` (GCC-format `.su` files) and `embcc inspect callgraph` are available. LTO, PIC and stack-protector assumptions remain excluded from RTOS builds. `-fshort-enums` is now available for C but remains prohibited by the RTOS ABI policy. `-fsanitize=undefined` traps in place. `-Os` exists; `-O0` code may not fit the ATmega328P. DWARF 4 now includes AVR functions/globals/lines, but no AVR `.debug_frame`; optimized debug builds can grow substantially. (`docs/manual/tools/embld.md`, `docs/manual/targets.md`, `docs/manual/invoking.md`)

**Consequences.**
- HW-006 still targets a GNU ld script plus an AVR compatibility `embld` option set (`-Ttext`/`-Tdata`/`--rom-limit`) from the same region model. Current EmbCC can consume the AVR script, so the option set is a retained design/compatibility target, not a missing-linker-script workaround. The generator remains planned; removing the second output requires a separate reviewed amendment.
- Kernel registries (device tables, trace points, log sites, partition tables) are orphan or named sections bracketed by linker symbols; this works identically under `embld` and GNU ld.
- Deferred-format logging (ADR-009) targets strings as **symbols whose address is the identifier**, placed in a `NOLOAD` dummy region. AVR linker scripts are now available; the tiny plain-text backend remains a footprint/design choice, not proof that deferred logging has been implemented or qualified on AVR.
- Stack sizing methodology (KRN-MEM-009) uses `-fstack-usage` plus the call graph on all three compilers.
- Footprint claims are made at `-Os` and `-O2`; `-O0` is a debug configuration only.
- Reproducible builds (BLD-004) are helped by EmbCC: `-g` records no directory, and output is deterministic by design.

## 8. Startup, vectors, and interrupts per target

| Target | Startup | Vector table | Handlers |
|---|---|---|---|
| Cortex-M | C `Reset_Handler`; hardware loads SP from the table | `const` array in `.vectors`/`.isr_vector` with `used` | Ordinary C functions (hardware saves the caller-saved set); `interrupt` attribute accepted as a no-op; context switch and first-thread start are `naked` |
| RISC-V | C `_start` after an `embld` `-Tstack` stub or a `.S` entry | `mtvec` set by startup | Trap entry in `.S` (save caller-saved registers, call C, `mret`); `interrupt` attribute refused |
| AVR | `.S` boot: set SPH/SPL, clear r1, copy `.data`, zero `.bss` | 26 `jmp` entries at address 0, weak `__vector_n` | `signal` (interrupts stay off) or `interrupt` (re-enabled at entry) |

(`docs/manual/embedded.md`)

**Consequence.** The architecture-port contract documents the startup form per target as above; SoC startup on Cortex-M stays in C.

## 9. Tools around the compiler

| Tool | What it is | Use in EmbLinkRTOS |
|---|---|---|
| `embld` | The linker (see §7) | Reference linker for EmbCC builds |
| `embdbg` | DWARF 4 reader, symbolizer, crash analyzer, TUI and GDB remote client, now including AVR register layout | AVR tool support is available; RTOS-awareness via the planned debug descriptor and full target sessions still need their own evidence |
| `embsvd` | CMSIS-SVD reader with nested clusters/arrays/derivedFrom; device header, startup/linker outputs and structured `--json` | HW-004 can build on the implemented JSON export; mapping it to curated RTOS SoC YAML/schema remains an unimplemented adapter |
| `embflash` | Flash client through GDB servers (OpenOCD, pyOCD, J-Link, Black Magic Probe, QEMU) | Available upstream; RTOS board/programmer integration remains to be tested |
| `embas`, `embar`, `embread`, `embls`, `embidx` | NASM-syntax x86-64 assembler; archiver; ELF and EMBX reader; listing and index tools | Build support |
| EmbBuild | A **typed-manifest build** (`.ebm`: `name`, `kind`, `inputs`, `args`, `output` per target) walked by EmbLinkOS's EmbBuild; manifests are generated, with header closures derived by the compiler; a host reference walker exists (`tools/embbuild-run.sh`) | Answers Q20: EmbBuild exists as a format and walker, not an IDE build system. The CMake reference build (BLD-001) gains a target that emits an `.ebm` manifest, so EmbLinkRTOS builds on EmbLinkOS itself |
| EMBX | EmbLinkOS's native, capability-carrying executable format, written by `embld --embx --cap NAME` from the same linked layout as the ELF | Design input for partitions: the per-partition manifest of granted capabilities (03 §8) should use EmbLinkOS's capability vocabulary where the concepts coincide, so tooling can be shared; MCU boot images stay MCUboot-compatible (ADR-008) |
| QEMU harnesses | `lm3s6965evb`, `mps2-an386`, `mps2-an500`, `mps2-an505`, micro:bit (Armv6-M), RISC-V `virt`, AVR, MIPS `malta`, with semihosting exit | Added to the CI emulation layer alongside Renode (SIM-002) |

EmbFlash is now developed upstream; the 2026-10-07 absence is obsolete. Existing OpenOCD/pyOCD, `picotool`/UF2 and `avrdude` workflows remain useful. The RTOS `emb` facade (BLD-005), board-specific flashing and HIL integration remain planned and must be verified separately.

## 10. Gaps to track in EmbCC

The original ten-item compiler backlog is implemented upstream within its stated scope: (1) Armv8-M stack limits, (2) AVR scripts, (3) AVR `__flash`/program-memory support, (4) AVR DWARF, (5) AVR debugger layout, (6) small RV32/AVR atomics, (7) soft-float CMSE, (8) M23/Baseline, (9) SVD clusters/arrays/JSON, and (10) Cortex-M/RV32 C++ without exceptions. The [closure matrix and local evidence](../engineering/embcc-capability-review.md) preserve each item's identity and remaining limits; do not erase the historical snapshot or infer complete RTOS qualification.

Current integration work is the missing EmbCC `EMB_LAYOUT_MARKER`, native target runtime/link/threading configuration, AVR flash/registration macros, RTOS YAML adaptation, C++ header/runtime checks and pinned compiler/port conformance jobs. Compiler TLS remains a shared embedded instance; default wide-atomic helpers are privileged local-mask operations. CMSE FPU/stack-crossing restrictions, AVR C++ refusal, debug unwinding/size limits and unrun physical measurements remain explicit.

## 11. Toolchain matrix for 1.0

| Architecture | EmbCC | GCC | Clang |
|---|---|---|---|
| native (host) | Compiler targets available; upstream reports a macOS RTOS experiment with injected layout marker. Current local Linux RTOS build stops at that missing marker; no active CI leg | yes | yes |
| avr | Compiler/script/flash/debug capabilities available; full RTOS run unqualified | avr-gcc | Clang AVR (experimental) |
| cortex_m | Compiler M0+/M3/M4/M4F/M7/M23/M33 targets; upstream M3/M4 RTOS experiments are scoped reports, not current CI qualification | arm-none-eabi-gcc | Clang with `--target=arm-none-eabi` |
| riscv | RV32/RV64 compiler targets; upstream RV32 RTOS experiment reported, current RTOS compiler leg unqualified | riscv-none-elf-gcc | Clang |

Every leg builds the same sources. Compiler-specific attribute and builtin spellings live only in `include/emb/compiler/` (BLD-002). Differences recorded here are the first contents of that directory's EmbCC header.
