# Static analysis

The coding standard names the static analyzers and how a finding is handled (CODING-STANDARD §12, §13). The engineering system makes them a merge gate (05 §5). This page says what runs today, on what code, and what is still missing. clang-tidy and cppcheck both run in CI's `static-analysis` job, which the `CI merge gate` requires.

## clang-tidy: a merge gate

CI's `static-analysis` job runs clang-tidy 18 with the root `.clang-tidy`. The checks, their reasons and the naming rules live only in that file. The job is one of the jobs the `CI merge gate` requires, and a single finding fails it.

**What it reads:** the C files of the product code, the code that ships in an image: `kernel/`, `arch/`, `boards/`, `soc/`, `drivers/`, `subsys/`, `compatibility/` and `examples/`. It also reads the headers those files include, by `HeaderFilterRegex`. Tests, samples, host tools and generated sources are out of scope.

**In which configurations:** `#if` selects different code by profile, port and option, so the same files are analyzed in 14 builds. Each is compiled by Clang, because clang-tidy reads GCC's flags poorly:

| Build | What it adds |
|---|---|
| `native-clang` | the checked base profile |
| native tiny, release, periodic, timeslice with tickless | the tiny scheduler, release checks, the periodic clock and time slicing |
| `cm0-microbit-clang` | Armv6-M |
| `cm4-an386-clang`, `cm4-an386-release-clang` | Armv7E-M with an FPU, checked and release |
| `cm33-an505-clang` | Armv8-M Mainline |
| `rv32-virt-clang` | RV32 |
| `avr-uno-clang` | AVR |
| NUCLEO-F446RE suite, Pico 2 W suite | the STM32F4 and RP2350 SoCs and boards |
| the reference controller | the STM32 drivers and `examples/controller` |

A finding reported by several builds is listed once, with the builds that reported it.

**Running it locally:** configure and build the trees as the job does, then run:

```sh
python3 tools/analysis/clang_tidy.py --clang-tidy clang-tidy-18 build/native-clang build/cm4-an386-clang ...
```

On macOS, Homebrew's `llvm@18` provides `clang-tidy` 18.1.8. Use version 18, which CI uses; another version enables different checks.

**Exit status:** 0 with no finding, 1 with any, 2 when clang-tidy could not run.

### The first run

When the gate was added (2026-11-08), the tree had 155 distinct findings across the builds. Most came from two checks whose advice conflicts with rules the standard already sets. Those checks are now off tree-wide, each with its reason in `.clang-tidy`:

- **`bugprone-casting-through-void` (56 findings).** The check is new in clang-tidy 18. The kernel casts through `void *` on purpose: to an object from its storage union (CS-4.5) and in `EMB_CONTAINER_OF` (MISRA Dev CS-12: 11.4, 18.4). The direct cast is what `-Wcast-align` reports, and the check's suggested alternative, `reinterpret_cast`, is C++ only.
- **`hicpp-no-assembler` (76 findings).** CS-1.5 allows inline assembly in the ports and the compiler layer, port headers included. clang-tidy judges a header by the file that includes it, so it cannot tell a port header from kernel code. `scripts/check-banned.py` enforces CS-1.5 by path instead.

The rest:

- **Naming (21 findings).**
  - Symbols that linker scripts define (`__data_start`, `__emb_retained_start`) and avr-libc's `__ATTR_PROGMEM__`: their names are not the project's to choose. The naming rules now ignore them.
  - Port helper macros ending in `_` (`EMBN_STR_`): the naming rules now accept them, as they already accepted `EMB_*_`.
  - `prio_write` in the Cortex-M port was a lowercase macro on Armv7-M and later. It is now a function, as it already was on Armv6-M.
- **One analyzer report, `clang-analyzer-core.NullDereference` in `embk_list_remove()`, reached from `workq_main()`.** The analyzer pops a node. After calls it cannot see into, it assumes the same removed node is first on the list again, but a removed node is on no list. This is recorded as a deviation at the site, with the reason.
- **One error in the runner:** it tried to analyze an assembly file. It now takes C files only.

- **One finding that only CI's Linux run reported, `clang-analyzer-core.NonNullParamChecker` in the native port.** It read `getenv("EMB_NATIVE_ASYNC_US")` twice: once to test for NULL, then again to parse it. glibc declares `strtoul()`'s argument non-null and macOS's headers do not, so only the Linux analysis could see the problem. The port now reads the variable once. Host headers differ in what they tell the analyzer, which is one reason the gate runs where CI builds, on Ubuntu 24.04, and not only on a developer's machine.

After these changes, the 14 builds give 0 findings.

## cppcheck: a merge gate

The same `static-analysis` job then runs cppcheck 2.22.0 over the same 14 builds, through `tools/analysis/cppcheck.py`. It reads the same product code, and a single finding fails the job.

**Version:** Ubuntu 24.04 ships cppcheck 2.13, and versions differ in what they report. CI builds 2.22.0 from its tagged source, pinned by SHA-256, and caches the build. Homebrew's `cppcheck` is 2.22.0 too.

**Checks:** the `warning`, `style`, `performance` and `portability` groups, with cppcheck's GNU library knowledge.

**The compiler, emulated.** cppcheck defines none of a compiler's macros: by itself it sees no `__GNUC__`, `__clang__`, `__ARM_ARCH` or `__x86_64__`. The compiler layer would then take its generic header, and the layout probe its `#error`. For each build, the tool asks the compiler, with that build's target flags, for its predefined macros (`cc -dM -E`). cppcheck reads them as a forced include. It then analyzes the code each build compiles, in the configuration the build selects. Each unit's own `-D` and `-I` come from `compile_commands.json`.

**Turned off tree-wide:** `.cppcheck-suppressions` at the root, like `.clang-tidy`, lists each check that is off, with its reason. The tool refuses an entry that names a file, or that has no reason. Two checks are off: `constParameterPointer` and `constVariablePointer`, advisory MISRA 8.13. cppcheck judges each configuration alone, so it asks for `const` where another configuration writes through the pointer. It also cannot see writes through an intrusive node (`EMB_CONTAINER_OF`).

**Running it locally:** build the trees, then run:

```sh
python3 tools/analysis/cppcheck.py build/cm4-an386 build/rv32-virt build/avr-uno ...
```

It works with GCC or Clang builds. **On macOS, the native build gives one syntax error**, in `kernel/exec/exec.c`. Mach-O names a section's bounds with assembler labels (`<emb/compiler/macho.h>`), which cppcheck cannot parse. CI analyzes native code on Linux, where the native build is ELF. The cross builds are ELF on every host.

### The first run

When the gate was added (2026-11-08), a first run on the native build alone, before the compiler was emulated, found the generated `storage.h` did not parse. With the compiler emulated, the 14 builds gave 46 distinct findings: the macOS syntax error above, the 25 of the two `const` checks, and 20 others. What each became:

- **The generated `<emb/storage.h>` did not parse** (`syntaxError`). Its union members carried `EMB_ALIGNED(n)` after the declarator. That works for GCC's `aligned` attribute, but C11's `_Alignas`, the generic compiler's fallback, is a specifier and comes first. The generator now writes it first, as SPEC-001 §6.2 shows. GCC, Clang and EmbCC (host, Arm and AVR targets) accept it there with the same size and alignment.
- **Configuration constants that looked like mistakes.**
  - On AVR, objects need alignment 1, so the checks of the caller's storage were `% 1` and always true (`moduloofone`, 6 sites). `embk_is_aligned()` now does all ten such checks, in one place.
  - On ports with one interrupt level, `level <= EMB_IRQ_LEVEL_MAX` was `level <= 0` on an unsigned value (`unsignedLessThanZero`). It is now `level < EMB_IRQ_LEVEL_MAX + 1u`.
  - The AVR port chose Timer1's prescaler with a `switch` on a configured constant (`unreachableSwitchCase`). It now chooses with `#if`.
- **Bounds the linker defines** (`comparePointers`, `subtractPointers`, 5 sites): the start-up copy of `.data` and clearing of `.bss` on Cortex-M, the init table's walk, and the retained area's size on Cortex-M and RV32. The two bounds of one region are two `extern` arrays to C. This is recorded as a deviation of MISRA 18.2 and 18.3 at each site, and in CODING-STANDARD §12.
- **The init table's bounds are weak** (`knownConditionTrueFalse`). With no entry they are undefined and their address is 0, which an array declaration cannot express. The test is needed; it is recorded at the site as a false positive.
- **`owned_remove()` in the mutex code** (`nullPointerRedundantCheck`). After its loop, the slot holds the mutex or NULL. It now tests the slot for NULL, not for the mutex.
- **Two variables declared wider than they are used** (`variableScope`): now declared in their block.

After these changes, the 14 builds give 0 findings.

### The MISRA addon

CODING-STANDARD §12 names `cppcheck --addon=misra` where the rule texts are available. The addon needs the MISRA C:2023 rule texts, which are licensed and are not in the tree. It is not run.

## Not covered

- **Xtensa (ESP32):** its Clang is Espressif's own build, which the `static-analysis` job does not install. `xt_port.c` is analyzed by neither tool.
- **EmbCC's own analyses:** they run where EmbCC builds (the `embcc` job). They are not part of this gate.
- **Host tools and tests:** they are hosted code outside the standard's scope (CS-1.2, §12's table).
