# EmbLinkRTOS Coding Standard

**Status:** In force from the first source file of Milestone 1 (07 §2). Named by 05 §1.6; every code review and every CI static-analysis gate (05 §5) applies it.
**Applies to:** every C file under `include/`, `kernel/`, `arch/`, `soc/`, `boards/`, `drivers/`, `subsys/`, `compatibility/`, `tests/`, `samples/`, and the C parts of `tools/`. Python tooling follows `tools/README.md` conventions and is not covered here.
**Precedence:** SPEC-001 governs everything a public header contains and how public functions are named and annotated; this standard governs how all code is written. Where document 09 records an EmbCC limit, this standard turns it into a rule so one source serves EmbCC, GCC, and Clang.

The rules are numbered `CS-<section>.<n>` so a review comment, a deviation record, or a static-analysis finding can name one.

---

## 1. Language and dialect

- **CS-1.1** Code compiles as C11 with `-std=c11 -pedantic` on GCC and Clang and as the single EmbCC dialect (09 §3). Nothing newer than C11 is used except through the portability layer (SPEC-001 §10). C17 clarifications are welcome; C23 features are not.
- **CS-1.2** The kernel, ports, SoC and board code, and drivers are **freestanding**: they include only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, `<stdatomic.h>` through `<emb/atomic.h>`, `<limits.h>`, and project headers. No `<stdio.h>`, `<stdlib.h>`, `<string.h>` (use `embk_memcpy`, `embk_memset` from `kernel/include/embk/util.h`, which map to the compiler builtins), `<assert.h>`, `<signal.h>`, `<setjmp.h>`, `<time.h>`, `<math.h>`, `<errno.h>`. Tests and host tools may use the host C library.
- **CS-1.3** Banned everywhere: `_Thread_local`, `__thread` (09 §3; KRN-THR-014), variable-length arrays, `alloca`, computed `goto`, `setjmp`/`longjmp`, `__int128`, the `cleanup` attribute, constructor priorities, `-fshort-enums`, LTO, `-fPIC`, stack protectors (SPEC-011 §15). A tree-wide script (`scripts/check-banned.py`) fails CI on the keywords and attributes.
- **CS-1.4** No variadic functions. The logging macros are the only variadic surface and expand to deferred-format calls (SPEC-001 §12).
- **CS-1.5** Inline assembly appears only under `arch/` and in `include/emb/compiler/`, following SPEC-011 §15 (naked functions or `.S` files for multi-block sequences, no labels in templates, no callee-saved registers named or clobbered, `"memory"` clobbers on critical-section primitives).
- **CS-1.6** Compiler-specific attributes, builtins, and pragmas appear only in `include/emb/compiler/<name>.h`. Generic code uses the `EMB_*` portability macros of SPEC-001 §10 and nothing else; `#ifdef __GNUC__`, `__clang__`, `__EMBCC__` outside that directory is a review rejection (BLD-002).

## 2. Undefined and implementation-defined behavior

The policy is that no construct relies on undefined behavior, and every implementation-defined behavior the code depends on is listed here and verified by a static assertion or a configure-time probe.

- **CS-2.1** Signed integer overflow never occurs. Arithmetic that may wrap is done on unsigned types; arithmetic that may exceed the range is range-checked first or performed in a wider type with an explicit saturating conversion. Tick arithmetic in the 32-bit profile wraps on `uint32_t` by design and is compared through `embk_tick_before()` (SPEC-003 §5.2), never with `<`.
- **CS-2.2** Shifts: the left operand is unsigned, the count is a constant or a value proven below the width, and a shift that would move a 1 out of the type is a bug, not a truncation. Bit masks are formed as `(UINT32_C(1) << n)` or `((uint8_t)1u << n)` on the intended width, never as `1 << n`.
- **CS-2.3** No null pointer is dereferenced, and no pointer arithmetic leaves the object. Pointer arithmetic is array indexing on an array of known length; the only exceptions are `EMB_CONTAINER_OF` for intrusive nodes and the stack-frame construction in `emb_arch_context_init`, both recorded in §12.
- **CS-2.4** Strict aliasing is respected: no object is accessed through a pointer to an incompatible type. Byte views use `unsigned char *`. Type punning of register files and frames uses structs declared for the purpose; punning between value types uses `embk_memcpy`. `-fno-strict-aliasing` is not used to paper over violations.
- **CS-2.5** Every variable is initialized before it is read. Structures passed to the kernel are initialized by `*_attr_default()` and then modified, never partially assigned.
- **CS-2.6** No data races. Every object accessed from more than one context is either (a) protected by a named lock domain (critical section, scheduler lock domain, object lock) stated in the comment on its declaration, (b) a C11 atomic accessed through `<emb/atomic.h>`, or (c) a single-writer sequence-locked pair (the 64-bit clock, SPEC-003 §3.1). `volatile` is not a synchronization mechanism (§5).
- **CS-2.7** Division and remainder have a nonzero divisor, proven or checked. Integer division by a non-power-of-two constant is avoided on hot paths of the tiny profile (09 §7).
- **CS-2.8** Alignment: pointers are cast to a more strictly aligned type only through the storage unions of SPEC-001 §6.2 or after an explicit alignment check that is a misuse fault on failure.
- **CS-2.9** Implementation-defined behavior relied upon, asserted in `kernel/embk_sanity.c`: two's complement integers; `CHAR_BIT == 8`; `int` is at least 16 bits (status codes fit, SPEC-001 §4); `uintptr_t` exists and round-trips object and function pointers on every port that stores one in a handle; right shift of a negative value is never performed; `sizeof(bool) == 1`; plain `char` signedness is never relied on (bytes are `uint8_t` or `unsigned char`).
- **CS-2.10** Function pointers and object pointers are not mixed. The one conversion, a thread entry address written into an initial stack frame, is done by the port in a documented place (§12).

## 3. Integer types and conversions

- **CS-3.1** Stored data (struct fields, globals, tables, ABI structures, anything the debug descriptor exports) uses fixed-width types from `<stdint.h>`. `int` and `unsigned int` are used for `emb_status_t`, loop counters, and short-lived locals; `size_t` for sizes and byte counts; `uintptr_t` only for handles and address arithmetic inside ports.
- **CS-3.2** Every narrowing conversion is an explicit cast preceded by the reason the value fits (a comment, a range check, or a static assertion on the source range). Builds use `-Wconversion -Wsign-conversion`, and a cast is never added only to silence a warning.
- **CS-3.3** Signed and unsigned operands are not mixed in one expression. Comparisons are between operands of the same signedness; a loop over an array uses an unsigned index.
- **CS-3.4** Unsigned constants carry the `u` suffix or a `UINT32_C`-style macro; a constant combined with a fixed-width operand matches its width.
- **CS-3.5** Enumerations are `int`-sized on EmbCC (09 §3): an enum type appears in a struct only as a fixed-width integer field (SPEC-001 §6.3, §8), and an enum value stored into one is range-checked once at the API boundary.
- **CS-3.6** `bool` holds only `true` or `false`; it is never used in arithmetic, never compared with `== true`, and never derived from a non-zero test without `!= 0` or a logical operator.
- **CS-3.7** Bit-fields are not used in any ABI, exported, or multi-context structure; packing is done with explicit masks and shifts.
- **CS-3.8** Floating point does not appear in the kernel, ports, or drivers' control paths. Where a driver class needs it, the class specification says so and the code is built with `-Wdouble-promotion`.

## 4. Memory, stacks, and recursion

- **CS-4.1** The kernel allocates nothing. Objects live in caller-provided storage (KRN-OBJ-003). A driver or subsystem that needs memory takes it from a pool or a configured allocator through the public API, never from a hidden static heap.
- **CS-4.2** No recursion in the kernel, ports, drivers, or any function reachable from an interrupt handler (MISRA Rule 17.2). The priority-inheritance walk and the timeout list walk are loops with declared bounds (`CONFIG_EMB_PI_MAX_DEPTH`, the number of armed nodes).
- **CS-4.3** Stack usage is bounded and known: every kernel and port function is compiled with `-fstack-usage`, a local array larger than 32 bytes needs a comment stating why it is on the stack, and the per-port documentation lists the worst-case kernel call depth from a thread and from a handler (KRN-MEM-009, SPEC-011 §10).
- **CS-4.4** File-scope mutable state is `static`, grouped in one struct per module (`embk_sched_state`, `embk_time_state`), and declared with its lock domain in the comment. Per-CPU state lives in `embk_cpu_t` (SPEC-002 §2). Tests and samples may use `g_` globals.
- **CS-4.5** Object storage sizes are generated (ADR-006); the kernel verifies with `EMB_STATIC_ASSERT` that the real object fits, and never casts storage to an object type without that assertion in the same translation unit.
- **CS-4.6** Large `const` tables that would cost SRAM under EmbCC on AVR (09 §2) are declared `EMB_FLASH_CONST` and read through `EMB_FLASH_READ_*`, or compiled out on the tiny profile (SPEC-012 §8).

## 5. `volatile`, atomics, and barriers

- **CS-5.1** `volatile` is used for exactly two things: memory-mapped device registers, accessed through the `EMB_REG8/16/32(addr)` accessors of `<emb/compiler.h>`, and `.noinit` crash-record fields read after a reset. It never appears in a public header (SPEC-001 §8) and never on a variable shared between threads or with an interrupt handler.
- **CS-5.2** Shared state is protected by the lock domains of §2.6. The compiler barrier `EMB_COMPILER_BARRIER()` is used only inside the critical-section and switch primitives; generic code gets its ordering from those primitives and from `<emb/atomic.h>`.
- **CS-5.3** Atomics are C11 atomics through `<emb/atomic.h>`, word-sized or narrower, with an explicit memory order on every operation. No 64-bit atomics on 32-bit targets, nothing wider than a byte on AVR (09 §6); the tiny profile's atomics are the critical section, which the header makes explicit.
- **CS-5.4** Device memory ordering beyond what C11 fences provide uses `emb_arch_mb()` and friends (SPEC-011 §8), only in drivers and ports, with a comment naming the hardware rule that requires it.

## 6. Bounded loops and real-time paths

- **CS-6.1** Every loop on a real-time path (anything annotated `@time` in SPEC-001 §9, every interrupt handler, every critical section) has a bound that is a constant, a configuration value, or the size of a structure, and the bound is named in the loop's comment or in the function's `@time` tag.
- **CS-6.2** `for (;;)` is reserved for thread bodies, the idle loop, and the panic path. A `while` loop whose condition depends on data external to the function states what bounds it.
- **CS-6.3** Busy-waiting on hardware appears only in ports and drivers, with a cycle or iteration bound and a timeout path; a kernel-aware context never spins on another thread.
- **CS-6.4** Critical sections contain one list operation or one state transition (SPEC-004 §5.1), never a loop over a data-dependent set, except the expiry loop of SPEC-003 §5.3, whose bound is the number of expired nodes and which is named in the port's latency documentation.

## 7. Control flow

- **CS-7.1** Braces on every `if`, `else`, `for`, `while`, `do`, including single statements.
- **CS-7.2** Early `return` is permitted for argument and context validation at the top of a function and for the fast path of an operation; after the first lock is taken, a function has one exit through its unlock (a forward `goto out` to a single cleanup label is permitted for this and for nothing else; MISRA 15.1 and 15.5 deviation, §12).
- **CS-7.3** `switch` always has a `default`; a `case` that falls through is marked `EMB_FALLTHROUGH;` on its own line; `switch` on an enum lists every enumerator or has a `default` that faults in checked builds.
- **CS-7.4** No assignment inside a condition or a function argument; no comma operator; no side effects in the operands of `&&`, `||`, `?:`, or in a macro argument.
- **CS-7.5** The conditional operator is used only to select between two values of the same type, never for control flow.
- **CS-7.6** `break` and `continue` are used sparingly, never more than one `break` per loop except in a `switch`.

## 8. Functions

- **CS-8.1** One purpose per function, at most about 80 lines of body, at most six parameters; beyond that, pass a struct.
- **CS-8.2** Every function has a prototype: non-static functions in exactly one header, static functions declared before use at file scope. `-Wmissing-prototypes -Wstrict-prototypes` are on. `void` is written for empty parameter lists.
- **CS-8.3** Input pointers are `const`-qualified. Output parameters are pointers named `out_*` and are written only on success unless the documentation says otherwise; on failure a handle output is `EMB_HANDLE_NULL`.
- **CS-8.4** Functions are `static` unless another translation unit needs them. `static inline` in a public header is limited to trivial accessors (SPEC-001 §8). `EMB_ALWAYS_INLINE` is used only for critical-section primitives, the clock read, and switch helpers whose latency is published.
- **CS-8.5** A function that returns `emb_status_t` returns every code its documentation lists and no other; a caller checks the result or casts it to `(void)` with a comment saying why the result does not matter.
- **CS-8.6** No function pointer is called without a null check at the registration point; the call site then assumes it valid.

## 9. Naming

SPEC-001 §2 and §3 fix the public and port namespaces. In addition:

- **CS-9.1** Kernel-private functions, types, and globals use `embk_<module>_<name>`; the module word is the directory under `kernel/` (`sched`, `wait`, `time`, `sync`, `thread`, `notify`, `object`, `exec`).
- **CS-9.2** File-local identifiers are lower case with underscores and carry no prefix. A file-local function that implements one public function is named after it with the prefix dropped and a verb kept (`lock_fast_path`).
- **CS-9.3** Macros and enumerators are upper case; macro parameters are lower case; a macro that expands to a statement is a `do { ... } while (0)`.
- **CS-9.4** Type names end in `_t`; a struct tag equals its typedef name without `_t` when both exist (`struct emb_mutex_attr` / `emb_mutex_attr_t`).
- **CS-9.5** Boolean-valued functions and fields read as predicates: `is_`, `has_`, `can_`, or a past participle (`suspended`, `armed`).
- **CS-9.6** Identifiers are unique in their first 31 characters, contain no `embos` in any case (SPEC-001 §2), and avoid C++ keywords (SPEC-001 §8).
- **CS-9.7** Source files are named `<module>_<topic>.c` under their directory (`kernel/sched/sched_bitmap.c`, `arch/avr/avr_switch.c`); private headers live in `<dir>/include/embk/` for the kernel and `arch/<arch>/include/` for ports.

## 10. Headers, includes, and macros

- **CS-10.1** Public headers follow SPEC-001 §8 exactly: `EMB_<PATH>_H` guards, `extern "C"`, C11 and C++17 clean, only the permitted standard headers, no `volatile`, no bit-fields in ABI structs, fixed-width members with explicit padding.
- **CS-10.2** Private headers use the guard `EMBK_<PATH>_H` and are not included by anything outside their directory tree; the include-path isolation of 02 §5 is enforced by a CI script (05 §3).
- **CS-10.3** Include order: the file's own header, other `<emb/...>` public headers, `<embk/...>` private headers, standard headers, with each group sorted. Includes appear only at file scope and never inside `extern "C"`.
- **CS-10.4** A function-like macro parenthesizes every parameter use and its whole expansion, evaluates each argument at most once, and is used only where a function cannot do the job: token pasting, type-generic selection, compile-time structure literals for the time types (SPEC-001 §7.3, because EmbCC's inliner declines struct-valued functions, 09 §7), and definition helpers (`EMB_THREAD_DEFINE`).
- **CS-10.5** Configuration symbols are read only from `<emb/config.h>`. Every boolean symbol is generated as `0` or `1`, every integer and string symbol has a value, and code tests them with `#if CONFIG_EMB_X` (never `#ifdef`); `-Wundef` makes a misspelled symbol a build error.
- **CS-10.6** Conditional compilation is kept at file granularity where possible (a feature's file is excluded from the build) and otherwise wraps whole functions; `#if` inside a function body is a review item that needs a reason.
- **CS-10.7** Constants are `enum` members or `#define`s with a type-suffixed literal; `static const` objects are not used for constants that must be compile-time (array bounds, `case` labels).

## 11. Comments, documentation, and annotations

- **CS-11.1** Every file starts with the SPDX and copyright lines of `CONTRIBUTING.md`, then a one-paragraph comment stating what the file implements and which specification sections it follows.
- **CS-11.2** Every public function carries the kernel-doc comment with the mandatory tags of SPEC-001 §9. Every `embk_` function carries at least `@ctx`, `@lock` (the lock domain it expects held, or `none`), and `@time`.
- **CS-11.3** Every shared field and every file-scope object states its lock domain in its declaration comment (§2.6).
- **CS-11.4** Comments explain why, not what; a comment that restates the code is removed in review. Commented-out code is not committed. `TODO` and `FIXME` are not committed; a known gap is a `FUTURE(ADR-nnn)` marker or an open question in 08.
- **CS-11.5** Requirement identifiers are cited in the code at the point that implements them (`/* KRN-WAIT-005: result before READY */`) so the traceability generator (05 §6) finds them.

## 12. MISRA C:2023 orientation and recorded deviations

The code follows the MISRA C:2023 Mandatory and Required rules unless a deviation below applies. Advisory rules are followed where they do not conflict with SPEC-001. A deviation not listed here is a review rejection; adding one is a change to this document with its rationale and its scope.

| Rule | Deviation | Scope and rationale |
|---|---|---|
| 1.2 Language extensions | Permitted through the portability layer only | Attributes (`naked`, `section`, `used`, `weak`, `always_inline`, `aligned`, `packed`), inline assembly, `__builtin_*`: all behind `EMB_*` macros in `include/emb/compiler/` and inside `arch/` (CS-1.5, CS-1.6). Every macro has a documented no-extension fallback. |
| 2.x Unused code | `EMB_UNUSED` parameters and `EMB_USED` tables | Port entry points with contract-fixed signatures; linker-bracketed registration tables that no code references. |
| 11.4, 11.6 Pointer and integer conversions | Permitted at the handle boundary and in `EMB_CONTAINER_OF` | Handles are `uintptr_t` words (SPEC-001 §6.1); the one conversion each way is in `kernel/object/object_handle.c`. Intrusive lists recover the container with `EMB_CONTAINER_OF` (one macro, one definition). |
| 11.1 Function pointer conversions | One site per port | `emb_arch_context_init` writes an entry address into a stack frame; the port documents the site. |
| 15.1 `goto` | Forward `goto out` to a single cleanup label | Release-the-lock-once exit shape (CS-7.2). |
| 15.5 Single exit | Early return after validation and on the fast path | CS-7.2; keeps hot paths short and the lock structure visible. |
| 17.2 Recursion | None | Followed strictly (CS-4.2). |
| 18.4 Pointer arithmetic | Array indexing only, plus the two sites of CS-2.3 | |
| 20.7 Macro parenthesization | Followed, except token-pasting and type-name parameters | `EMB_DECLARE_HANDLE(name)`, `EMB_THREAD_DEFINE`; a parameter used as a token is documented in the macro's comment. |
| 21.x Standard library | Freestanding; none used in kernel, ports, drivers | CS-1.2; tests and host tools are hosted code and out of this scope. |
| Dir 4.9 Function-like macros | Permitted for the cases of CS-10.4 | EmbCC inliner limits and C++17 lack of designated initializers (SPEC-001 §6.3, §7.3). |
| Dir 4.12, Rule 21.3 Dynamic memory | None in kernel | CS-4.1. |

Static analysis: `clang-tidy` with the project `.clang-tidy`, `cppcheck --addon=misra` where the rule texts are available to the contributor, EmbCC's own diagnostics, and the compiler warning set of §13. A finding is fixed or recorded as a deviation with a `/* MISRA Dev CS-12: rule, reason */` comment at the site; a bare suppression is rejected.

## 13. Toolchain flags and formatting

- **CS-13.1** Warning set, every leg, warnings are errors: `-Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wsign-conversion -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations -Wvla -Wcast-align -Wcast-qual -Wdouble-promotion -Wformat=2 -Wswitch-default -Wimplicit-fallthrough -Wnull-dereference -Wredundant-decls -Wnested-externs -fno-common`. EmbCC accepts the subset it knows and ignores the rest (09 §3). Per-port flags (`-fstack-usage`, `-ffunction-sections -fdata-sections`) are in the CMake toolchain files.
- **CS-13.2** Layout is whatever `.clang-format` at the repository root produces; CI rejects a file that `clang-format` would change. Four-space indentation, no tabs, 100 columns, Linux-style braces (function braces on their own line, control-statement braces on the same line), pointer star attached to the name.
- **CS-13.3** `.clang-tidy` at the repository root lists the enabled checks and the naming rules; a check is disabled only tree-wide in that file with a comment, never per line, except for the recorded deviations of §12 with a `NOLINT(<check>)` that names the deviation.
- **CS-13.4** `.editorconfig` sets encoding (UTF-8, no BOM), line endings (LF), final newline, and trailing-whitespace removal for every file type.

## 14. Tests

- **CS-14.1** A test file is named `test_<topic>.c` and lives under `tests/<layer>/`; each test case is `EMB_TEST(<group>_<name>)` and cites the requirements it verifies with `EMB_TEST_REQ("KRN-SCH-001", ...)` so the traceability matrix (TEST-009) is generated.
- **CS-14.2** Tests synchronize with kernel primitives and the virtual clock, never with host sleeps or spin counts; a test that depends on wall time is a benchmark and lives under `tests/benchmarks/`.
- **CS-14.3** A test checks behavior through the public API and the trace stream; it does not reach into kernel structures except through the debug descriptor or a `tests/kernel/` white-box test that says so in its name.
- **CS-14.4** Every misuse path has a checked-build test expecting the fault and a release-build test expecting the status (TEST-010).

## 15. Review checklist

A reviewer confirms, for every change: the rules above with the section numbers cited where something is borderline; the `@ctx`, `@lock`, and `@time` tags match the body; every new shared field names its lock domain; every narrowing cast has its reason; every loop on a real-time path names its bound; the model (`tools/model/`) and the conformance suite were updated for any semantic change (05 §5); the footprint report of the tiny configuration did not regress.
