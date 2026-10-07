# 09 - EmbCC Toolchain Profile

**Status:** Fact base, verified against the EmbCC repository (EmbLinkOs/EmbCC at commit 387eb23, 2026-10-07). Each fact names the EmbCC document it comes from so it can be re-checked when EmbCC moves. The *consequence* lines are the EmbLinkRTOS design rules that follow; they are normative and are cross-referenced from documents 03, 05, 07, and 08.

---

## 1. What EmbCC is

EmbCC is a C and C++ compiler written in C99 with its own preprocessor, per-target assemblers, linker (`embld`), archiver (`embar`), SVD tool (`embsvd`), and debugger (`embdbg`). One process preprocesses, compiles, assembles, and links. It runs on macOS and Linux and self-hosts. Its primary target is EmbLinkOS on x86-64; the embedded targets are bare-metal C targets with EmbCC's compiler runtime (`librt.a`) and no C library. (`README.md`, `docs/internals/status.md`)

## 2. Targets that matter to EmbLinkRTOS

| Family | Triples | Notes for the RTOS |
|---|---|---|
| Cortex-M, Armv6-M | `thumbv6m-none-eabi` (Cortex-M0, M0+, M1) | Thumb-1 lowering; divide, 64-bit multiply, and atomics are `librt.a` calls; the atomic helpers mask with PRIMASK, which does nothing in unprivileged Thread mode, so an isolated profile on Armv6-M would need its own atomics |
| Cortex-M, Armv7-M | `thumbv7m-none-eabi` (M3) | Reference port target |
| Cortex-M, Armv7E-M | `thumbv7em-none-eabi`, `thumbv7em-none-eabihf` (M4, M4F, M7 with `-mcpu=cortex-m7` for FPv5-D16) | Hard-float objects do not link with soft-float ones; one `librt.a` per float ABI |
| Cortex-M, Armv8-M Mainline | `thumbv8m.main-none-eabi`, `thumbv8m.main-none-eabihf` (M33) | **TrustZone CMSE is not supported** (`-mcmse` unknown, CMSE attributes ignored). Cortex-M23 (Armv8-M Baseline) is refused by name. Availability of `msplim`/`psplim` in inline asm is stated both ways in `docs/manual/inline-asm.md`; verify with a probe compile before relying on stack-limit registers |
| RISC-V | `riscv32-unknown-elf` (RV32IMAC, ilp32), `riscv64-unknown-elf` (RV64IMAC, lp64) | ISA fixed, no `-march`; soft float only; `medany`; the RP2350's Hazard3 cores run this code |
| AVR | `avr` (ATmega328P only, no `-mmcu`) | `double` is 32-bit; `const` data and string literals live in **RAM** (copied from flash at startup); `__flash` is not implemented |
| MIPS32 | `mipsel-none-elf` (MIPS32r2, PIC32 class) | A candidate fifth architecture port; not planned |
| Hosts | `x86_64-linux-gnu`, `x86_64-apple-darwin`, `aarch64-*`, `x86_64-windows-gnu` (incomplete) | C++ supported on x86-64 and AArch64 only |

All targets are little-endian. C is supported on every target; C++ is refused on Cortex-M, RV32, MIPS32, and AVR. (`docs/manual/targets.md`, `docs/manual/embedded.md`)

**Consequence.** EmbCC already covers every 1.0 architecture of the roadmap: AVR, Cortex-M from M0+ to M33, RISC-V 32 and 64. The v0.1 assumption that AVR support had to be added to EmbCC is obsolete. EmbCC is in the M1 compiler matrix from the first commit.

## 3. Language level

- One dialect: C17 plus GNU extensions plus part of C23; `-std=` is accepted and ignored, `__STDC_VERSION__` is `201710L`. `_Static_assert`, `_Generic`, `_Noreturn` supported; `_Alignas` partial; `_Atomic` for integer and pointer types only; `<stdatomic.h>` and `__atomic_*`/`__sync_*` builtins provided. (`docs/manual/c-language.md`)
- **`_Thread_local` and `__thread` compile to one shared instance on every embedded target**, without an error. (`docs/internals/status.md` §Thread-local storage)
- `__STDC_HOSTED__` is 1 even with `-ffreestanding`. Empty parameter list `()` always means no parameters. A non-void function that can reach its closing brace is an error. Block-scope tags and typedefs stay visible to the end of the translation unit. (`docs/manual/c-language.md`)

**Consequences.**
- The C11 minimum of ADR-023 is enforced by the GCC and Clang legs of the matrix with `-std=c11 -pedantic`; EmbCC cannot enforce it.
- The kernel and every port **never use `_Thread_local` or `__thread`**. Per-thread data is kernel TLS slots only (KRN-THR-013, KRN-THR-014). A static-analysis rule bans the keywords tree-wide.
- The kernel does not test `__STDC_HOSTED__`.

## 4. Attributes and builtins

Supported where the RTOS needs them: `naked` (Cortex-M, RISC-V, MIPS32, AVR), `interrupt` (Cortex-M: accepted, no code change; AVR: implemented; RISC-V: refused), `signal` (AVR), `section` (file scope only), `used`, `weak`, `alias` (functions), `aligned` (objects and struct definitions; not typedefs; not AVR locals), `packed`, `constructor` (no priority), `noreturn`, `always_inline`, `pcs`. Provided builtins include `__builtin_memcpy`/`memset`/`memmove`/`memcmp`, `__builtin_trap`, `__builtin_expect`, the atomics, `__builtin_sqrt`, `__builtin_fabs`.

Refused: `cleanup`, `constructor(N)`, `weakref`, `ifunc`, `target`, `vector_size`, `mode`, `transparent_union`, `aligned` on a typedef, `packed` or `aligned` on an enum, `section` on a block-scope variable; `__builtin_avr_*`, `__builtin_debugtrap`, `__builtin_assume`, Clang's `__c11_atomic_*`. (`docs/internals/status.md` §Attributes, §Builtins)

**Consequences.**
- Device and subsystem initialization order is a generated table (DRV-001), never constructor priorities.
- No scope-guard macros built on `cleanup`.
- Alignment goes on object declarations and struct definitions, never typedefs. Kernel object storage types (ADR-006) are structs, so this is natural.
- Register-file-wide bit operations use the `emb_arch_` layer (`clz`, `ctz`, `popcount`) with compiler-portability fallbacks; the exact `__builtin_clz` family availability is verified by a probe at configure time.

## 5. Inline assembly and naked functions

- Inline asm is assembled by EmbCC's own assembler per target with a **fixed instruction vocabulary** (documented per target). Labels are not accepted inside inline asm; branch targets are `.+N`. Only `r`/`m`/`i`-style constraints; callee-saved registers may not be named or clobbered in a template (r4-r11 on Cortex-M, s0-s11 on RISC-V, r2-r17 and Y on AVR). An asm output wider than a register is refused.
- Cortex-M vocabulary covers what a kernel needs: `mrs`/`msr` on `primask`, `basepri`, `basepri_max`, `faultmask`, `control`, `msp`, `psp`; `cpsid`/`cpsie`; `dsb`/`dmb`/`isb`; `wfi`/`wfe`/`sev`; `ldrex`/`strex`; `ldm`/`stm` with writeback; `push`/`pop`; `svc`; `bx`/`blx`; `vpush`/`vpop`/`vldm`/`vstm`/`vmov`; `it` blocks; `bkpt`. Missing: `ldrd`/`strd`, `clrex`, `vmrs`/`vmsr`, byte and halfword exclusives.
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
| RV32 | 1 to 4 bytes | 4 bytes only (no 1- or 2-byte forms) |
| RV64 | 1 to 8 bytes | 4 and 8 bytes |
| AVR | 1 byte | **none** |

Eight-byte atomics on 32-bit targets, and anything wider than one byte on AVR, are compile errors. Computed `goto` is refused on all embedded targets. `__int128` does not exist on 32-bit targets. (`docs/internals/status.md` §Target-specific refusals)

**Consequences.**
- 64-bit kernel time is read and written under a critical section or a sequence lock, never as an atomic 64-bit object (KRN-TIM-016).
- Kernel atomic state is word-sized. Flags packed into bytes are manipulated as words or under a critical section.
- The tiny profile's atomics are interrupt masking by definition; the portability layer makes that explicit rather than relying on compiler fallbacks.
- No computed-goto dispatch anywhere in the kernel.

## 7. Linking, sections, and images

- `embld` links ARM, RV32, RV64, MIPS32, AVR, and x86-64 objects; AArch64 needs an external linker. Static links only, no PLT, `ET_EXEC`, W^X segments.
- Without a script: `-Ttext`, `-Tdata`, `-Tstack` (RISC-V entry stub), `--rom-limit`, `-e`, `--gc-sections`, `-Map`, `--print-memory-usage`; `.vectors`/`.isr_vector` placed first; orphan sections bracketed by `__start_NAME`/`__stop_NAME` or `__NAME_start`/`__NAME_end`.
- **GNU ld linker scripts (`-T`) are supported for ARM and RISC-V only, not AVR or MIPS.** The supported subset is broad: `MEMORY`, `SECTIONS`, `KEEP`, `PROVIDE`, `ALIGN`, `AT>`, `(NOLOAD)`, `/DISCARD/`, `ASSERT`, `INCLUDE`, `REGION_ALIAS`, `EXTERN`, sorting, fill, the usual expression functions. Refused: `PHDRS`, `OVERLAY`, `INSERT`, output section types other than `NOLOAD`.
- `-ffunction-sections`, `-fdata-sections`, `--gc-sections`, `-fstack-usage` (GCC-format `.su` files) and `embcc inspect callgraph` are available. **No LTO, no `-fPIC`, no `-fstack-protector`, `-fshort-enums` refused**; `-fsanitize=undefined` traps in place. `-O3` is `-O2`; `-Os` exists; `-O0` code may not fit the ATmega328P. Debug info is DWARF 4 only; **on AVR `-g` produces no functions, variables, or lines** (recorded defect). (`docs/manual/tools/embld.md`, `docs/manual/embedded.md`, `docs/internals/status.md`)

**Consequences.**
- The hardware generator emits, for every board, both a GNU ld script (consumed by GCC, Clang, and `embld` on ARM and RISC-V) and an **`embld` option set** (`-Ttext`/`-Tdata`/`--rom-limit`) for AVR. The two are generated from the same region model so they cannot drift (HW-006).
- Kernel registries (device tables, trace points, log sites, partition tables) are orphan or named sections bracketed by linker symbols; this works identically under `embld` and GNU ld.
- Deferred-format logging (ADR-009) interns strings as **symbols whose address is the identifier**, placed in a `NOLOAD` output section in a dummy memory region, the way `defmt` does, because `embld` does not accept other non-loaded output section types. On AVR, where no script exists, the tiny profile's plain-text log backend is used.
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
| `embdbg` | DWARF 4 reader, symbolizer, crash-report analyzer, TUI, and **GDB remote serial protocol client** that talks to QEMU's stub or OpenOCD; knows register layouts for ARM M-profile, RV32, RV64, x86-64, AArch64; **not AVR** | This is the EmbDebug of the v0.1 spec. The kernel debug descriptor (ADR-011) is its input for RTOS awareness. HIL on AVR uses `avr-gdb` or a simulator stub until `embdbg` learns AVR |
| `embsvd` | Reads a CMSIS-SVD file; writes a CMSIS-shaped device header, a C startup file with a weak-handler vector table, and a CubeMX-shaped linker script; `--list`/`--show`; refuses clusters and unusual register arrays by name; ISO C, standalone | The natural base for the SoC-description importer (HW-004): extend it with an output mode that emits the EmbLinkRTOS SoC YAML, rather than writing a second SVD parser |
| `embas`, `embar`, `embread`, `embls`, `embidx` | NASM-syntax x86-64 assembler; archiver; ELF and EMBX reader; listing and index tools | Build support |
| EmbBuild | A **typed-manifest build** (`.ebm`: `name`, `kind`, `inputs`, `args`, `output` per target) walked by EmbLinkOS's EmbBuild; manifests are generated, with header closures derived by the compiler; a host reference walker exists (`tools/embbuild-run.sh`) | Answers Q20: EmbBuild exists as a format and walker, not an IDE build system. The CMake reference build (BLD-001) gains a target that emits an `.ebm` manifest, so EmbLinkRTOS builds on EmbLinkOS itself |
| EMBX | EmbLinkOS's native, capability-carrying executable format, written by `embld --embx --cap NAME` from the same linked layout as the ELF | Design input for partitions: the per-partition manifest of granted capabilities (03 §8) should use EmbLinkOS's capability vocabulary where the concepts coincide, so tooling can be shared; MCU boot images stay MCUboot-compatible (ADR-008) |
| QEMU harnesses | `lm3s6965evb`, `mps2-an386`, `mps2-an500`, `mps2-an505`, micro:bit (Armv6-M), RISC-V `virt`, AVR, MIPS `malta`, with semihosting exit | Added to the CI emulation layer alongside Renode (SIM-002) |

EmbFlash does not exist in the EmbCC repository; its status is still open (Q19).

## 10. Gaps to track in EmbCC

These are the EmbCC limitations EmbLinkRTOS designs around today and would benefit from if closed. They are listed so they can become EmbCC issues, in the order they matter to the roadmap.

1. Armv8-M stack-limit registers (`msplim`, `psplim`) in inline asm: confirm, document consistently. Needed for KRN-MEM-010 on Armv8-M in M3.
2. Linker scripts for AVR (`-T`). Removes the dual-output generator path.
3. `__flash` or an equivalent program-memory qualifier on AVR. Today every `const` table costs SRAM on a 2 KB part.
4. DWARF on AVR (currently empty). Needed for `embdbg` and the debug descriptor on the first hardware target.
5. `embdbg` AVR register layout for the remote client.
6. 1- and 2-byte atomic read-modify-write on RV32 (and any on AVR), or a documented helper path.
7. TrustZone CMSE (`-mcmse`) for the secure-side build of Armv8-M; the non-secure RTOS image does not need it.
8. Cortex-M23 (Armv8-M Baseline) support, for the low-end Armv8-M parts.
9. `embsvd`: SVD clusters and arbitrary register arrays; a structured output mode for the hardware description.
10. C++ on the 32-bit embedded targets, for the C++ wrappers; not blocking (C-first).

## 11. Toolchain matrix for 1.0

| Architecture | EmbCC | GCC | Clang |
|---|---|---|---|
| native (host) | x86-64 and AArch64 Linux and macOS; kernel sources compile; the host-thread port layer may need GCC or Clang depending on EmbCC's libc threading, to be verified in M1 | yes | yes |
| avr | yes (ATmega328P) | avr-gcc | Clang AVR (experimental) |
| cortex_m | M0+/M3/M4/M4F/M7/M33, soft and hard float | arm-none-eabi-gcc | Clang with `--target=arm-none-eabi` |
| riscv | RV32IMAC, RV64IMAC | riscv-none-elf-gcc | Clang |

Every leg builds the same sources. Compiler-specific attribute and builtin spellings live only in `include/emb/compiler/` (BLD-002). Differences recorded here are the first contents of that directory's EmbCC header.
