# API - Public API Conventions

Group `API`. Design: `docs/specs/SPEC-001-api-conventions.md`. Decisions: ADR-002, ADR-003, ADR-006, ADR-014, ADR-023. API-001 to API-005 originate in `docs/architecture/05-engineering-system.md` §1.4 and are restated here as the authoritative copy.

---

### API-001  Return type discipline
**Statement.** Every public function shall return `emb_status_t`, or a value type whose every value is documented, or `void` only when the function cannot fail. No public function shall report an error through a global or thread-local variable.
**Rationale.** One error-reporting path keeps call sites uniform and works from ISRs and across partitions; EmbCC compiles thread-local storage to a single instance on embedded targets, which rules out `errno`-style state (09 §3).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/apidoc` rejects a public prototype whose return type is not `emb_status_t`, a documented value type, or `void`; a header grep forbids `errno`.
**Trace.** SPEC-001 §4.4; ADR-002; 05 §1.2

### API-002  Timeouts on blocking functions
**Statement.** Every public function that may block shall take an `emb_timeout_t` as its last parameter and shall accept `EMB_NO_WAIT` and `EMB_WAIT_FOREVER`. Every such function shall have an absolute-deadline variant named with the `_until` qualifier taking an `emb_instant_t` as its last parameter.
**Rationale.** Unbounded waits hidden in signatures are the first cause of hangs; absolute deadlines are what periodic real-time code needs to avoid drift (KRN-TIM-011).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/apidoc` checks that every `@blocks timeout` function ends with `emb_timeout_t` and has a `_until` sibling ending with `emb_instant_t`.
**Trace.** SPEC-001 §3, §7.4; ADR-002

### API-003  Machine-readable contract annotations
**Statement.** Every public function shall carry the annotation tags `@ctx`, `@blocks`, `@time`, `@owns`, `@config`, `@req`, `@since`, and `@stable` in its documentation comment, with values from the grammar in SPEC-001 §9. A missing or malformed mandatory tag shall fail the build's documentation gate.
**Rationale.** Documentation, static analysis, and the conformance-suite generator must read the same contract, or they drift from each other and from the code.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/apidoc` parses every header under `include/emb/` and fails on a missing or invalid tag; its JSON output validates against `tools/apidoc/schema.json`.
**Trace.** SPEC-001 §9; 05 §1.4

### API-004  Header portability
**Statement.** Every public header shall compile without warnings under the project warning set as C11 (`-std=c11 -pedantic` on GCC and Clang) and as C++17, on EmbCC, GCC, and Clang, for every supported target.
**Rationale.** The headers are the contract with three compilers and two languages; EmbCC does not enforce a standard level, so GCC and Clang do (09 §3).
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/api/headers/` compiles each header alone and the umbrella header, in C and C++, across the compiler matrix.
**Trace.** SPEC-001 §8; ADR-023

### API-005  One API for all privilege levels
**Statement.** The public API shall be identical in names, signatures, and documented semantics for privileged and unprivileged callers. The build shall select a direct call or a system-call stub per partition without changing the header a caller includes.
**Rationale.** Application source must move between the tiny, base, and isolated profiles unchanged (principle 9).
**Status.** Accepted 2026-10-07
**Verification.** Test: the conformance suite builds and passes once as privileged and once inside an unprivileged partition on the isolated profile, from the same sources.
**Trace.** SPEC-001 §5.3; 03 §8.1; KRN-CAP-001

### API-006  Namespace reservation
**Statement.** Identifiers beginning with `emb_`, `EMB_`, or `Emb` shall be reserved for the project. Identifiers beginning with `__emb_` or `_EMB_` shall appear only in generated headers and the compiler-portability layer. No project identifier shall contain the sequence `embos` in any letter case.
**Rationale.** A predictable prefix lets tooling key on the API, and `embOS` is an existing commercial RTOS trademark (ADR-002).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: identifier scan in CI over `include/`, `kernel/`, `arch/`, `drivers/`, `subsys/`.
**Trace.** SPEC-001 §2; ADR-002

### API-007  Identifier grammar
**Statement.** Every public function shall be named `emb_<subject>_<verb>[_<qualifier>]` with the verb taken from the standard verb table in SPEC-001 §3. A subject introducing a verb not in the table shall document it in that subject's specification. Public types shall be named `emb_<subject>[_<noun>]_t`; public macros and enumerators `EMB_<SUBJECT>_<NAME>`.
**Rationale.** Uniform names make the API learnable from one example per verb and let generated documentation group operations by subject.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/apidoc` checks every public identifier against the grammar and the verb table.
**Trace.** SPEC-001 §3

### API-008  Parameter order
**Statement.** Public function parameters shall appear in the order: object handle, input parameters, output parameters (pointer parameters named with the prefix `out_`), then the timeout or deadline.
**Rationale.** A fixed order removes a class of call-site mistakes and lets the annotation checker locate the timeout mechanically (API-002).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/apidoc` checks parameter order and `out_` naming.
**Trace.** SPEC-001 §3

### API-009  No `_try` and no `_from_isr` variants
**Statement.** The public API shall not define functions whose names end in `_try` or `_from_isr`, nor any other duplicated variant whose only difference is the calling context or a zero timeout.
**Rationale.** A zero timeout is `EMB_NO_WAIT`; ISR safety is a documented property of the single function (ADR-003). Duplicates double the surface and the documentation.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: identifier scan.
**Trace.** SPEC-001 §3, §5.2; ADR-003

### API-010  Stable status values
**Statement.** Each status code's numeric value shall be fixed when first released and shall never change or be reused. Kernel core codes shall occupy -1 to -31, kernel reserved -32 to -63, driver classes and subsystems -64 to -127, application-defined -128 to -255. No status value below -255 shall be defined.
**Rationale.** Status values cross the syscall boundary, appear in trace and crash records, and are stored by applications; they are ABI.
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/api/status/` asserts every value against a frozen table; Analysis: enumerators outside their range fail a header check.
**Trace.** SPEC-001 §4.2

### API-011  Status code set
**Statement.** The kernel shall define exactly the status codes `EMB_OK`, `EMB_EPERM`, `EMB_EINVAL`, `EMB_EBUSY`, `EMB_ETIMEDOUT`, `EMB_ECANCELED`, `EMB_EDESTROYED`, `EMB_EOWNERDEAD`, `EMB_ENOMEM`, `EMB_ENOTSUP`, `EMB_EIO`, `EMB_EFAULT`, `EMB_ENOENT`, `EMB_EEXIST`, `EMB_ESTATE`, `EMB_EOVERFLOW`, `EMB_ESTALE`, and `EMB_EINTR` with the values and meanings in SPEC-001 §4.2. A driver class or subsystem shall define its additional codes in its own specification within its range.
**Rationale.** A closed, small set keeps error handling uniform and each code's meaning unambiguous across subsystems.
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/api/status/`; Inspection of each subsystem specification for range compliance.
**Trace.** SPEC-001 §4.2

### API-012  One code for an unsatisfied wait
**Statement.** A blocking function whose awaited condition does not become true within the allowed wait shall return `EMB_ETIMEDOUT`, including when the allowed wait is `EMB_NO_WAIT`.
**Rationale.** One outcome, one code; the wait protocol produces the single result `TIMEOUT` for both cases (03 §3.5).
**Status.** Accepted 2026-10-07
**Verification.** Test: conformance tests for every blocking primitive with `EMB_NO_WAIT` and with a finite timeout.
**Trace.** SPEC-001 §4.2; KRN-WAIT-004

### API-013  Error precedence
**Statement.** When more than one error applies to a call, the function shall report in this order: `EMB_EPERM`, then `EMB_EINVAL`, then `EMB_ESTALE`, then state and resource errors, then wait-result errors.
**Rationale.** Callers and tests can rely on a deterministic code when several faults coincide.
**Status.** Accepted 2026-10-07
**Verification.** Test: conformance misuse tests that combine two faults.
**Trace.** SPEC-001 §4.4

### API-014  Context classes
**Statement.** Every public function shall belong to exactly one context class declared by its `@ctx` tag: ISR-safe (`thread isr`, optionally `prekernel`), thread-only (`thread`), or pre-kernel (`prekernel`, optionally `thread`). An ISR-safe function shall never block and shall have a `@time` bound that names its variables.
**Rationale.** The context class is the contract that replaces duplicated ISR variants (ADR-003) and is what static analysis checks at call sites.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/apidoc` validates tag combinations; Test: each `thread isr` function is exercised from an ISR in the conformance suite.
**Trace.** SPEC-001 §5.2; KRN-IRQ-010, KRN-IRQ-011

### API-015  Misuse behavior
**Statement.** API misuse (wrong context, blocking with a nonzero timeout while the scheduler is locked or in a critical section, invalid handle, null required pointer, out-of-range constant, non-owner unlock, destroy with waiters without `ABORT_WAITERS`, misaligned or undersized storage) shall raise a kernel fault in checked builds and return the status in SPEC-001 §5.3 in release builds. Runtime conditions (`EMB_ESTATE`, `EMB_ESTALE`, `EMB_EBUSY` for resources, `EMB_ENOMEM`, `EMB_ENOTSUP`, and the wait results) shall return their status in both build kinds.
**Rationale.** Fail loudly in development and predictably in production, without ever silently converting misuse into different behavior (principle 11).
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/conformance/misuse/` runs every row of the table in both build kinds (TEST-010).
**Trace.** SPEC-001 §5.3; 01 §3 principle 11

### API-016  No kernel fault from an unprivileged caller
**Statement.** In isolated profiles, a call entering the kernel from an unprivileged partition shall have every check of API-015 performed at the system-call boundary and shall return a status to the caller; it shall never raise a kernel fault.
**Rationale.** A partition must not be able to take the system down by misusing the API (KRN-CAP-002, KRN-PART-003).
**Status.** Accepted 2026-10-07
**Verification.** Test: the misuse suite runs inside an unprivileged partition and observes statuses only.
**Trace.** SPEC-001 §5.3; KRN-CAP-002

### API-017  Misuse faults are attributable
**Statement.** A kernel fault raised by API misuse shall record the fault class, the public function identifier, and the index of the offending argument in the crash record.
**Rationale.** A fault without its cause costs a reproduction; the crash record exists to avoid that (FLT-001).
**Status.** Accepted 2026-10-07
**Verification.** Test: provoke each misuse class on the native port and decode the crash record.
**Trace.** SPEC-001 §5.3; 03 §10.2

### API-018  Handle representation
**Statement.** Every kernel object handle shall be a struct type containing a single `uintptr_t`, declared with `EMB_DECLARE_HANDLE`, distinct per object type, passed and returned by value. Applications shall operate on handles only by copying, by `EMB_HANDLE_EQ`, and by `EMB_HANDLE_IS_NULL`. The same declaration shall serve the pointer, tagged-pointer, and table-index representations.
**Rationale.** One word costs the same as a pointer on every ABI, the struct keeps object types apart at compile time, and the representation can change by profile without changing a signature (ADR-014).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: header check that every `*_t` object handle is declared via `EMB_DECLARE_HANDLE`; Test: `sizeof(handle) == sizeof(uintptr_t)` on every target.
**Trace.** SPEC-001 §6.1; KRN-CAP-001

### API-019  Generated storage types
**Statement.** Every kernel object type shall have a generated storage type `emb_<object>_storage_t` and constants `EMB_<OBJECT>_STORAGE_SIZE` and `EMB_<OBJECT>_STORAGE_ALIGN` derived from the configured kernel layout. The kernel shall assert at compile time that the real object fits the generated size and alignment.
**Rationale.** Opaque objects and static allocation are both required (KRN-MEM-002, KRN-TCB-008); generated sizes reconcile them without compiler extensions (ADR-006).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `_Static_assert` in the kernel; Test: configuration-matrix build.
**Trace.** SPEC-001 §6.2; ADR-006

### API-020  Attribute structs
**Statement.** Construction options shall be passed in a plain struct `emb_<object>_attr_t` whose fields are fixed-width integers or pointers; a `NULL` attribute pointer shall select defaults; and a function `emb_<object>_attr_default()` shall fill a struct with the defaults.
**Rationale.** Fixed-width fields keep layout independent of enum size (EmbCC enums are `int`, 09 §4); the default function serves C++17, which lacks designated initializers.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: header check of attribute struct field types; Test: `attr_default` followed by `init` equals `init(NULL)` for every object.
**Trace.** SPEC-001 §6.3

### API-021  Construction and destruction
**Statement.** Every object type shall provide `emb_<object>_init(storage, attr, out_handle)` with context class pre-kernel and thread, which shall set `*out_handle` to `EMB_HANDLE_NULL` on failure, and `emb_<object>_destroy(handle)`. A compile-time `EMB_<OBJECT>_DEFINE` form shall declare storage and handle and register the object in the generated initialization table.
**Rationale.** Static definition without hand-written init code is how tables of objects stay in data, not code (principle 14).
**Status.** Accepted 2026-10-07
**Verification.** Test: conformance init/destroy and `_DEFINE` tests per object type.
**Trace.** SPEC-001 §6.4; KRN-OBJ-001, KRN-OBJ-003

### API-022  Distinct time types
**Statement.** The public API shall use three distinct struct types over `emb_tick_t`: `emb_instant_t` for points on the kernel monotonic clock, `emb_duration_t` for lengths of time, and `emb_timeout_t` for permitted waits, with the sentinels `EMB_NO_WAIT` and `EMB_WAIT_FOREVER`. Constructors `EMB_TICKS`, `EMB_NS`, `EMB_US`, `EMB_MS`, `EMB_SEC`, and `EMB_TIMEOUT` shall be macros usable in constant initializers.
**Rationale.** Passing an instant where a duration is expected is a silent real-time bug in scalar-typed APIs; the struct makes it a compile error (ADR-002). Macros avoid EmbCC's inliner limit on struct-valued helpers (09 §7).
**Status.** Accepted 2026-10-07
**Verification.** Test: negative compile tests that mixing the types fails; positive tests of constant initializers on all compilers.
**Trace.** SPEC-001 §7.1, §7.3; KRN-TIM-010

### API-023  Time conversion rules
**Statement.** The tick length shall be the configuration constant `CONFIG_EMB_TICK_NS`. Conversion from a duration to ticks shall round up; conversion from ticks to a duration shall be exact. A constant conversion that overflows `emb_tick_t` shall be a compile-time error; a runtime conversion shall saturate and the saturation shall be detectable by the caller.
**Rationale.** A wait must never be shorter than requested, and silent truncation is the overflow class KRN-TIM-007 forbids.
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/api/time/` conversion vectors including boundary and saturation cases in the 64-bit and 32-bit tick profiles.
**Trace.** SPEC-001 §7.2, §7.3; KRN-TIM-007

### API-024  Blocking time bounds
**Statement.** A blocking call with timeout `t` shall return no earlier than `t` after its start as measured on the kernel clock, and, absent satisfaction, no later than `t` plus one tick (or one timer resolution in tickless configurations) plus the scheduling latency of the caller's priority. `EMB_NO_WAIT` shall never block or yield. A `_until` deadline already in the past shall behave as `EMB_NO_WAIT`.
**Rationale.** These are the timing promises applications build periodic and watchdog logic on; they must be stated to be measured (KRN-RT-001).
**Status.** Accepted 2026-10-07
**Verification.** Test: timeout accuracy tests on the native port with virtual time; Demonstration: HIL latency benchmarks record the bound per board.
**Trace.** SPEC-001 §7.4; KRN-TIM-001, KRN-TIM-003, KRN-TIM-011

### API-025  Public header rules
**Statement.** Every public header shall have an include guard named `EMB_<PATH>_H`, wrap declarations in `extern "C"` for C++, include only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, and `<emb/...>` headers, and shall contain no variable-length arrays, no bit-fields in structs that cross the API, no anonymous structs, no `volatile`, no function definitions other than trivial `static inline` accessors, and no macro that evaluates an argument more than once. Pointers to input data shall be `const`. Structs that cross the API shall use fixed-width members with explicit padding.
**Rationale.** Headers are the ABI surface for three compilers and two languages (API-004); each rule removes a known portability or layout hazard, including those EmbCC documents (09 §3, §4).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: header lint in CI (guard names, include list, forbidden constructs); Inspection for padding.
**Trace.** SPEC-001 §8

### API-026  No C++ keywords
**Statement.** No public identifier, parameter name, or struct member shall be a C++17 keyword.
**Rationale.** The headers are included from C++ unchanged (API-004).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: identifier scan against the C++17 keyword list.
**Trace.** SPEC-001 §8

### API-027  Enumerations in the API
**Statement.** A struct that crosses the API shall store enumeration values in fixed-width integer fields, not in `enum`-typed fields. An `enum` type shall appear in a prototype only when its value set is closed and fully documented.
**Rationale.** EmbCC's enums are always `int`-sized and `-fshort-enums` is refused; layouts must not depend on it (09 §4).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: header lint.
**Trace.** SPEC-001 §6.3, §8

### API-028  Annotation export
**Statement.** The annotation tool shall export one JSON object per public function containing every tag of API-003 with parsed values, validated against a published schema, and the reference manual, the static-analysis rules, and the conformance-suite generator shall consume that export rather than re-parsing headers.
**Rationale.** One parser, one truth; three consumers that each parse would disagree in corner cases.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: schema validation in CI; Inspection that the three consumers import the export.
**Trace.** SPEC-001 §9; 05 §1.4

### API-029  Compiler portability layer
**Statement.** Generic kernel, port-independent, driver, and subsystem code shall express compiler-specific attributes and builtins only through the macros of SPEC-001 §10, defined in `include/emb/compiler/<compiler>.h`. Each macro shall have a defined fallback so that an unknown compiler compiles with the generic header.
**Rationale.** Scattered `__attribute__` and `__builtin_` uses are how toolchain independence is lost (PORT-ABI-002, BLD-002).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: CI grep forbids `__attribute__`, `__builtin_`, and `__asm` outside `include/emb/compiler/` and `arch/`.
**Trace.** SPEC-001 §10; PORT-ABI-002; BLD-002

### API-030  Versioning and stability
**Statement.** The headers shall define `EMB_VERSION_MAJOR`, `EMB_VERSION_MINOR`, `EMB_VERSION_PATCH`, `EMB_VERSION`, and `EMB_API_VERSION`, and the kernel shall provide `emb_version_string()`. From release 1.0, a function tagged `@stable yes` shall keep its signature and documented semantics for all 1.x releases; a compatible addition shall increment `EMB_API_VERSION`; a deprecated function shall carry `EMB_DEPRECATED` and `@stable deprecated:<version>` and remain for at least one minor release; experimental functions shall live under `include/emb/experimental/`.
**Rationale.** Products on long support windows need a written compatibility promise (05 §7.1).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: API export diff between releases fails CI on an incompatible change to a `@stable yes` function; Inspection of release notes.
**Trace.** SPEC-001 §11; 05 §7

### API-031  Callbacks and variadics
**Statement.** Every public callback type shall document its execution context in the same terms as `@ctx`. No public function shall be variadic or take a variadic function, except the logging macros, which shall expand to deferred-format calls.
**Rationale.** Callback context confusion is a classic RTOS bug class; variadics defeat annotation and type checking and are poorly supported for inlining on EmbCC (09 §7).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: prototype scan for `...`; Inspection of callback documentation.
**Trace.** SPEC-001 §12; ADR-009

### API-032  No hidden error state
**Statement.** The kernel shall keep no global or per-thread last-error state. `emb_status_name()` shall be available when `CONFIG_EMB_STATUS_NAMES` is enabled and shall otherwise compile out.
**Rationale.** Hidden state is unsafe from ISRs and across partitions and is impossible on targets where thread-local storage is a single instance (09 §3).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: symbol scan of the kernel image for error-state objects; Test: `emb_status_name` presence per configuration.
**Trace.** SPEC-001 §4.3; KRN-THR-014
