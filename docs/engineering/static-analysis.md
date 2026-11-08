# Static analysis

The coding standard names the static analyzers and how a finding is handled (CODING-STANDARD §12, §13). The engineering system makes them a merge gate (05 §5). This page says what runs today, on what code, and what is still missing.

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

## cppcheck: not yet a gate

CODING-STANDARD §12 also names cppcheck, with its MISRA addon. It is not run in CI yet. A first run on the native build found a handful of findings in the kernel, with two setup problems still to solve:
- cppcheck does not emulate a compiler, so the compiler layer must be given one;
- its version must be pinned: Ubuntu 24.04 ships 2.13, Homebrew 2.22.

The MISRA addon is also still to be measured.

## Not covered

- **Xtensa (ESP32):** its Clang is Espressif's own build, which the `static-analysis` job does not install. `xt_port.c` is not analyzed.
- **EmbCC's own analyses:** they run where EmbCC builds (the `embcc` job). They are not part of this gate.
- **Host tools and tests:** they are hosted code outside the standard's scope (CS-1.2, §12's table).
