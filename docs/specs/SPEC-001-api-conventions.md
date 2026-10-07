# SPEC-001 - Public API Conventions

**Status:** Draft for review. Specification work item 1 of the roadmap (07 §3).
**Requirements:** `docs/requirements/API.md` (API-001 to API-032). This document is the design; the requirement file is the normative, testable statement of it.
**Decided by:** ADR-002 (namespaces, status enum, typed time), ADR-003 (single API with context classes), ADR-006 (generated storage types), ADR-014 (handles are capabilities), ADR-023 (C11 minimum, freestanding).
**Toolchain constraints applied:** document 09 (EmbCC: one C17 dialect, int-sized enums, struct-by-value inlining limits, no `_Thread_local`).

---

## 1. Scope

Everything a public header in `include/emb/` is allowed to contain, and how every public function is named, typed, documented, and annotated. Kernel internals (`embk_`) and the architecture-port contract (`emb_arch_`) follow the same conventions where they apply but are not public API.

## 2. Namespaces

| Prefix | Meaning | Visible to |
|---|---|---|
| `emb_` | Public functions, types, variables | Applications, middleware, compatibility layers |
| `EMB_` | Public macros, constants, enumerators | Same |
| `emb_arch_` / `EMB_ARCH_` | Architecture-port contract | Kernel and ports only |
| `embk_` / `EMBK_` | Kernel-private | Kernel only |
| `emb_drv_<class>_` | Driver class APIs | Drivers, subsystems, applications |
| `CONFIG_EMB_` | Configuration symbols (generated into `<emb/config.h>`) | Everyone |
| `__emb_` / `_EMB_` | Reserved for generated code and compiler-portability internals; never written by hand in application or kernel source | Generated headers |

Anything starting with `emb`, `EMB`, `Emb` is reserved for the project. No identifier may contain `embos` in any case (trademark proximity, ADR-002).

## 3. Identifier grammar

```
function   := "emb_" subject "_" verb [ "_" qualifier ]
subject    := object | service            e.g. thread, mutex, sem, event, notify, queue, pipe, pool,
                                          timer, work, workq, part, cap, mem, time, sched, irq, power, log, trace
verb       := see table                   one word, lower case
qualifier  := "until" | "all" | "any" | "front" | "n" | ...   one word, documented per subject
type       := "emb_" subject [ "_" noun ] "_t"
macro      := "EMB_" SUBJECT "_" NAME     upper case, underscores
enumerator := "EMB_" SUBJECT "_" NAME     same form as a macro
```

Standard verbs. A subject uses the verb from this table that matches the operation; a new verb needs a note in the subject's specification.

| Verb | Meaning | Examples |
|---|---|---|
| `init` | Construct in caller-provided storage; returns a handle | `emb_mutex_init` |
| `create` | Construct using a configured allocator (only when `CONFIG_EMB_DYNAMIC_OBJECTS`) | `emb_thread_create` |
| `destroy` | End the object's life; storage returns to the caller | `emb_mutex_destroy` |
| `start`, `stop`, `restart` | Begin, end, or re-arm an activity | `emb_thread_start`, `emb_timer_start` |
| `suspend`, `resume` | Externally pause and continue a thread | `emb_thread_suspend` |
| `join`, `detach`, `cancel`, `exit` | Thread lifecycle | `emb_thread_join` |
| `lock`, `unlock` | Mutex ownership | `emb_mutex_lock` |
| `take`, `give` | Semaphore count | `emb_sem_take` |
| `set`, `clear`, `wait` | Event flags, notifications | `emb_event_set`, `emb_notify_wait` |
| `send`, `recv` | Queues, ports | `emb_queue_send` |
| `read`, `write` | Byte streams, devices | `emb_pipe_read` |
| `alloc`, `free` | Pools and heaps | `emb_pool_alloc` |
| `submit`, `cancel` | Work items, driver requests | `emb_work_submit` |
| `get`, `set` | Attribute access | `emb_thread_get_priority`, `emb_thread_set_priority` |
| `now`, `sleep` | Time | `emb_time_now`, `emb_thread_sleep` |

Qualifiers: `_until` is the absolute-deadline form of a blocking verb (§7.4). `_all` and `_any` are event-wait modes. `_front` is send-to-front on a queue. There is no `_try` qualifier (pass `EMB_NO_WAIT`) and no `_from_isr` qualifier (ADR-003).

Parameter order is fixed: the handle first, then inputs, then outputs (pointers named `out_*`), then the timeout or deadline last.

## 4. Status codes

### 4.1 Type

```c
typedef int emb_status_t;
```

`int` is the natural return register on every ABI (16 bits on AVR, 32 bits elsewhere). Zero is success. Negative values are errors from the fixed table below. Positive values are operation-specific counts where the function's documentation says so (bytes transferred, items received); such a function documents its maximum.

### 4.2 Error table

Values are stable forever once released (API-010). The errno column is the mapping the POSIX compatibility layer uses; it is informative here.

| Name | Value | Meaning | errno |
|---|---|---|---|
| `EMB_OK` | 0 | Success | 0 |
| `EMB_EPERM` | -1 | Caller not permitted: wrong execution context, not the owner, insufficient capability rights | `EPERM` |
| `EMB_EINVAL` | -2 | Invalid argument: null required pointer, out-of-range value, invalid handle, misaligned or undersized storage | `EINVAL` |
| `EMB_EBUSY` | -3 | Object or resource is in use and the operation cannot proceed without waiting where waiting is not allowed (destroy with waiters in release builds, device busy) | `EBUSY` |
| `EMB_ETIMEDOUT` | -4 | The awaited condition did not become true within the allowed wait, including a zero wait (`EMB_NO_WAIT`) | `ETIMEDOUT` |
| `EMB_ECANCELED` | -5 | The wait ended because the calling thread was canceled | `ECANCELED` |
| `EMB_EDESTROYED` | -6 | The wait ended because the object was destroyed with `ABORT_WAITERS` | `EIDRM` |
| `EMB_EOWNERDEAD` | -7 | The mutex's previous owner terminated while holding it (configuration-dependent) | `EOWNERDEAD` |
| `EMB_ENOMEM` | -8 | Pool, heap, table, or capacity exhausted | `ENOMEM` |
| `EMB_ENOTSUP` | -9 | Not supported by this configuration, profile, target, or device | `ENOTSUP` |
| `EMB_EIO` | -10 | Hardware or transport error | `EIO` |
| `EMB_EFAULT` | -11 | Address outside the caller's accessible regions (isolated profiles) | `EFAULT` |
| `EMB_ENOENT` | -12 | No such object, device, or entry | `ENOENT` |
| `EMB_EEXIST` | -13 | Already exists, already initialized, already started | `EEXIST` |
| `EMB_ESTATE` | -14 | The object or thread is in a state that does not permit the operation (resume a thread that is not suspended, start a running timer) | `EINVAL` |
| `EMB_EOVERFLOW` | -15 | Value or size exceeds what can be represented or stored (time conversion saturated, message larger than the slot) | `EOVERFLOW` |
| `EMB_ESTALE` | -16 | Handle refers to an object generation that no longer exists | `ESTALE` |
| `EMB_EINTR` | -17 | Reserved for signal-like interruption of waits; not returned in 1.0 | `EINTR` |

Ranges: -1 to -31 kernel core (above); -32 to -63 reserved for the kernel; -64 to -127 driver classes and subsystems, each defined in that class's specification with the prefix `EMB_E` and a class word (for example `EMB_ECRC`); -128 to -255 application-defined, from `EMB_EAPP_BASE`. No value below -255 is ever defined, so a status always fits an `int8_t` if an application wants to store one compactly.

### 4.3 Declaration form

```c
/* <emb/status.h> */
typedef int emb_status_t;

enum emb_status_code {
    EMB_OK          =  0,
    EMB_EPERM       = -1,
    /* ... as the table ... */
    EMB_EINTR       = -17,
    EMB_EAPP_BASE   = -128
};

const char *emb_status_name(emb_status_t status);   /* "EMB_EINVAL"; compiled out when CONFIG_EMB_STATUS_NAMES=n */
```

The enum names the values; the function type stays `int` so that counts can share it. No `errno`, no thread-local error state (KRN-THR-014 forbids compiler TLS anyway).

### 4.4 Rules

- A function returns `emb_status_t`, or a value type whose documentation defines every value, or `void` only when it cannot fail (pure accessors, `emb_time_now`).
- A function that reports success and produces a value returns the status and writes the value through an `out_*` pointer.
- A function never returns a positive count *and* a distinct success code; if it returns counts, `EMB_OK` means zero.
- Error precedence when several apply: `EMB_EPERM` (context, rights) before `EMB_EINVAL` (arguments) before `EMB_ESTALE` before state and resource errors; the wait-result errors (`ETIMEDOUT`, `ECANCELED`, `EDESTROYED`) are exclusive of each other by the wait protocol (KRN-WAIT-004).

## 5. Execution contexts and misuse

### 5.1 Contexts

| Context | Definition | `emb_context()` returns |
|---|---|---|
| **thread** | Running on a thread stack after the scheduler started, including work-queue and software-timer callbacks | `EMB_CONTEXT_THREAD` |
| **isr** | Interrupt or exception handler at or below the kernel's masking level | `EMB_CONTEXT_ISR` |
| **prekernel** | Before `emb_kernel_start()` returns control to the first thread | `EMB_CONTEXT_PREKERNEL` |

Kernel-independent interrupts (above the kernel's masking level, 02 §6) may call nothing in the public API; `emb_context()` is not meaningful there.

Two **sub-states** of thread context restrict blocking: *scheduler locked* (`emb_sched_lock()` depth > 0) and *critical section* (`emb_irq_lock()` held). A blocking call with a nonzero timeout in either sub-state is misuse.

### 5.2 Context classes of functions

Every function belongs to exactly one class, stated in its `@ctx` annotation (§9):

| Class | `@ctx` | Callable from | Rules |
|---|---|---|---|
| **ISR-safe** | `thread isr` or `thread isr prekernel` | thread, isr, (prekernel) | Never blocks; O(1) or a named bound; may make a thread READY and set `reschedule_pending` |
| **Thread-only** | `thread` | thread | May block when the timeout is not `EMB_NO_WAIT`; with `EMB_NO_WAIT` it behaves as its non-blocking form and is still thread-only unless the annotation says `thread isr` |
| **Pre-kernel** | `prekernel` or `prekernel thread` | before start, (thread) | Object initialization, configuration, registration |

Convention: object `init` functions are `prekernel thread`; signal-type operations (`give`, `set`, `notify_set`, `work_submit`, `queue_send` with `EMB_NO_WAIT`) are `thread isr`; everything that can wait is `thread`.

A blocking function called from an ISR is misuse even with `EMB_NO_WAIT` unless its annotation includes `isr`. Rationale: the ISR-safe variant of an operation is a property of its implementation path (no ownership tracking, no priority inheritance), not of the timeout value; the annotation is the contract. Where a blocking operation has a sound non-blocking path from ISR context, the annotation says so (`emb_queue_send` with `EMB_NO_WAIT` is the canonical case and is annotated `thread isr`).

### 5.3 Misuse versus runtime condition

A **misuse** is a programming error in the caller that a correct program never makes. A **runtime condition** is an outcome a correct program must handle. The distinction decides the behavior in checked builds.

| Situation | Kind | Checked build (`CONFIG_EMB_CHECKED=y`) | Release build |
|---|---|---|---|
| Wrong context for the function's class | misuse | kernel fault `EMB_FAULT_API_CONTEXT` | `EMB_EPERM` |
| Blocking with nonzero timeout while scheduler locked or in a critical section | misuse | fault `EMB_FAULT_API_CONTEXT` | `EMB_EPERM` |
| Null required pointer, out-of-range constant, bad enum | misuse | fault `EMB_FAULT_API_ARGUMENT` | `EMB_EINVAL` |
| Invalid handle (null, wrong type tag) | misuse | fault `EMB_FAULT_API_HANDLE` | `EMB_EINVAL` |
| Stale handle (generation mismatch) | runtime condition in profiles that check it | `EMB_ESTALE` | `EMB_ESTALE` |
| Unlock by non-owner | misuse | fault `EMB_FAULT_API_OWNER` | `EMB_EPERM` |
| Destroy with waiters, no `ABORT_WAITERS` | misuse | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EBUSY` |
| Storage misaligned or too small | misuse | fault `EMB_FAULT_API_ARGUMENT` | `EMB_EINVAL` |
| Operation not valid in current object state | runtime condition | `EMB_ESTATE` | `EMB_ESTATE` |
| Timeout, cancel, destroyed-while-waiting, busy, no memory, unsupported | runtime condition | status | status |

**Isolated profiles.** A call from an unprivileged partition crosses the syscall boundary, where every check above is performed and *always* returns a status to the caller; an unprivileged partition can never cause a kernel fault by misusing the API (KRN-CAP-002). Faults in checked builds apply to privileged callers only.

Faults carry the API function identifier and the offending argument index into the crash record (03 §10.2).

## 6. Handles, storage, and attributes

### 6.1 Handles

A handle is one machine word wrapped in a per-object struct type so that the compiler keeps object types apart:

```c
#define EMB_DECLARE_HANDLE(name)  typedef struct name { uintptr_t raw; } name

EMB_DECLARE_HANDLE(emb_thread_t);
EMB_DECLARE_HANDLE(emb_mutex_t);
/* ... one per kernel object type ... */

#define EMB_HANDLE_NULL        { 0 }
#define EMB_HANDLE_IS_NULL(h)  ((h).raw == 0)
#define EMB_HANDLE_EQ(a, b)    ((a).raw == (b).raw)
```

The word holds a pointer (tiny profile), a tagged pointer with generation bits (base profile), or a capability-table index (isolated profiles); the API signature is the same in all three (KRN-CAP-001, ADR-014). Handles are passed by value, copied freely, and compared only with `EMB_HANDLE_EQ`. Nothing in application code dereferences a handle.

All three ABIs pass a one-word struct in a single register. EmbCC does not inline a function that takes or returns a struct (09 §7, optimizer gaps); public API functions are real calls anyway, and the handle helpers above are macros, so there is no cost.

### 6.2 Storage

Every object type has a generated storage type and size constant (ADR-006):

```c
/* generated into <emb/storage.h> from the configured kernel layout */
#define EMB_MUTEX_STORAGE_SIZE   16u
#define EMB_MUTEX_STORAGE_ALIGN  4u
typedef union emb_mutex_storage {
    emb_max_align_t align_;
    unsigned char   bytes_[EMB_MUTEX_STORAGE_SIZE];
} emb_mutex_storage_t;
```

The kernel verifies with `_Static_assert` that the real object fits the generated size and alignment. Storage belongs to the caller for the object's whole life and may be `static`, in a table, or on a long-lived stack frame; the kernel never frees it (KRN-OBJ-003).

### 6.3 Attributes

Construction options travel in a plain attribute struct. `NULL` means defaults.

```c
typedef struct emb_mutex_attr {
    const char *name;        /* optional, kept by pointer: must outlive the object; NULL allowed */
    uint8_t     protocol;    /* EMB_MUTEX_INHERIT (default) | EMB_MUTEX_CEILING | EMB_MUTEX_NONE */
    uint8_t     ceiling;     /* for EMB_MUTEX_CEILING */
    uint8_t     flags;       /* EMB_MUTEX_RECURSIVE, EMB_OBJ_ABORT_WAITERS, ... */
} emb_mutex_attr_t;

void emb_mutex_attr_default(emb_mutex_attr_t *out_attr);    /* C++17 has no designated initializers */
```

Attribute fields that hold enumeration values are fixed-width integers (`uint8_t`), not enum types, because enums are `int`-sized on EmbCC (`-fshort-enums` is refused) and public struct layout must not depend on that.

### 6.4 Construction and destruction

```c
emb_status_t emb_mutex_init(emb_mutex_storage_t *storage,
                            const emb_mutex_attr_t *attr,      /* NULL = defaults */
                            emb_mutex_t *out_mutex);
emb_status_t emb_mutex_destroy(emb_mutex_t mutex);
```

`init` is `prekernel thread`. On failure `*out_mutex` is `EMB_HANDLE_NULL`. `destroy` follows KRN-OBJ-001. Compile-time definition helpers exist for the common static case:

```c
EMB_MUTEX_DEFINE(name, attr_initializer...)   /* storage + handle variable + registration in the init table */
```

The `_DEFINE` form registers the object in a generated initialization table so it is constructed before `main` (or before the scheduler starts) without hand-written init code; the table is one of the bracketed sections `embld` and GNU ld both provide (09 §7).

## 7. Time

### 7.1 Types

```c
typedef uint64_t emb_tick_t;                       /* uint32_t when CONFIG_EMB_TICK_32BIT=y (tiny profile) */

typedef struct emb_instant  { emb_tick_t ticks; } emb_instant_t;   /* a point on the kernel monotonic clock */
typedef struct emb_duration { emb_tick_t ticks; } emb_duration_t;  /* a length of time */
typedef struct emb_timeout  { emb_tick_t ticks; } emb_timeout_t;   /* how long a call may wait */
```

Three structs over the same scalar: the compiler rejects an instant where a duration is expected, which is the class of bug this exists to prevent (drifting periodic loops, deadlines compared with durations). The cost is one or two registers per argument on every ABI. EmbCC's inliner declines functions taking or returning structs (09 §7), so every helper on these types that must be free is a macro, and the kernel converts to raw `emb_tick_t` once at the API boundary.

### 7.2 Tick unit

`CONFIG_EMB_TICK_NS` is the length of one tick in nanoseconds (default 1 000 000, one millisecond). In tickless configurations the kernel clock advances in the same unit from the hardware timer. All conversions are relative to this constant and are exact at compile time where the argument is a constant.

### 7.3 Constructors and conversions

```c
#define EMB_TICKS(n)   ((emb_duration_t){ .ticks = (emb_tick_t)(n) })
#define EMB_NS(n)      ((emb_duration_t){ .ticks = EMB_NS_TO_TICKS(n) })
#define EMB_US(n)      ((emb_duration_t){ .ticks = EMB_US_TO_TICKS(n) })
#define EMB_MS(n)      ((emb_duration_t){ .ticks = EMB_MS_TO_TICKS(n) })
#define EMB_SEC(n)     ((emb_duration_t){ .ticks = EMB_SEC_TO_TICKS(n) })

#define EMB_NO_WAIT       ((emb_timeout_t){ .ticks = 0 })
#define EMB_WAIT_FOREVER  ((emb_timeout_t){ .ticks = EMB_TICK_MAX })
#define EMB_TIMEOUT(d)    emb_timeout_from_duration(d)     /* saturates to EMB_TICK_MAX - 1 so a finite wait never reads as forever */

emb_instant_t  emb_time_now(void);                                       /* thread isr prekernel */
emb_instant_t  emb_instant_add(emb_instant_t t, emb_duration_t d);       /* saturating */
emb_duration_t emb_instant_sub(emb_instant_t later, emb_instant_t earlier);  /* 0 if later < earlier */
bool           emb_instant_before(emb_instant_t a, emb_instant_t b);     /* wrap-safe in the 32-bit profile */
uint64_t       emb_duration_to_ns(emb_duration_t d);                      /* saturating */
```

Rounding rule: a duration-to-ticks conversion rounds **up**, so that a wait of `EMB_MS(3)` on a 2 ms tick waits 2 ticks, never 1. A ticks-to-duration conversion is exact. Compile-time constant conversions that overflow `emb_tick_t` are compile errors (`_Static_assert` inside the macro where the argument is constant); runtime conversions saturate and the caller can detect saturation with `emb_duration_is_saturated()`.

C++17 wrappers expose the same three types as `constexpr` strong types with `std::chrono` conversions.

### 7.4 Blocking semantics

- A call with timeout `t` returns no earlier than `t` after the call's start as read on the kernel clock, and no later than `t` plus one tick plus the scheduling latency of the caller's priority. In tickless configurations with a high-resolution timer the extra tick is the timer's resolution.
- `EMB_NO_WAIT` never blocks and never yields.
- `EMB_WAIT_FOREVER` has no deadline; the wait ends only by satisfaction, cancel, or destroy.
- Every blocking function has an absolute form `..._until(..., emb_instant_t deadline)`; a deadline in the past behaves as `EMB_NO_WAIT`. Periodic work uses `emb_thread_sleep_until(next)` with `next = emb_instant_add(next, period)` so that drift does not accumulate (KRN-TIM-011).
- 64-bit time is read with `emb_time_now()`, which uses a sequence lock or critical section internally; no 64-bit atomic is involved (KRN-TIM-016).

## 8. Headers

```
include/emb/
  emb.h          umbrella: includes every public header below
  config.h       GENERATED from Kconfig: CONFIG_EMB_* as macros
  storage.h      GENERATED: EMB_*_STORAGE_SIZE/ALIGN and storage types
  version.h      EMB_VERSION_*, EMB_API_VERSION, emb_version()
  compiler.h     the portability macros (§10); includes compiler/<name>.h
  types.h        emb_tick_t, emb_max_align_t, EMB_DECLARE_HANDLE, handle macros
  status.h       emb_status_t, the error table, emb_status_name
  time.h         §7
  context.h      emb_context(), EMB_CONTEXT_*, emb_in_isr()
  thread.h  sched.h  mutex.h  sem.h  event.h  notify.h  cond.h
  queue.h  pipe.h  pool.h  timer.h  work.h  part.h  cap.h  mem.h
  irq.h  power.h  fault.h  log.h  trace.h
  compiler/      gcc.h  clang.h  embcc.h  (one per compiler, nothing else includes them directly)
```

Rules for every public header:

- Include guard `EMB_<PATH>_H` (`EMB_MUTEX_H`); no `#pragma once` (EmbCC drops unknown pragmas silently but `once` is honored; the guard is the portable form and is kept as the single mechanism).
- `#ifdef __cplusplus extern "C" {` around declarations.
- Compiles as C11 (`-std=c11 -pedantic` on GCC and Clang) and as C++17, with the project's warning set clean, on EmbCC, GCC, and Clang (API-004).
- Includes only `<stddef.h>`, `<stdint.h>`, `<stdbool.h>`, and other `<emb/...>` headers. No hosted libc headers.
- No function definitions except `static inline` helpers that are trivial accessors; no macros that evaluate an argument twice; no variable-length arrays; no bit-fields in ABI structs; no anonymous structs (not C++17); anonymous unions only where every compiler accepts them.
- Identifiers avoid C++ keywords (`new`, `delete`, `class`, `this`, `template`, `operator`, `export`, `namespace`, `typename`, `register`).
- Every struct that crosses the API has fixed-width members and explicit padding, so its layout is identical on all compilers for a target.
- `const` on every pointer to input data. `volatile` never appears in a public header.
- Enumerations appear as `enum` types in prototypes only where the full value set is closed and documented; in structs they are fixed-width integers (§6.3).

## 9. Machine-readable contract annotations

Each public function carries a documentation comment whose tagged lines are parsed by `tools/apidoc/` into `api.json`, consumed by the reference manual generator, the static-analysis rules (clang-tidy checks and EmbCC diagnostics for context misuse), and the conformance-suite generator (API-003).

```c
/**
 * emb_mutex_lock() - Acquire a mutex, waiting up to @timeout.
 * @mutex:   The mutex.
 * @timeout: How long to wait; EMB_NO_WAIT or EMB_WAIT_FOREVER allowed.
 *
 * @ctx      thread
 * @blocks   timeout
 * @time     O(1) acquire; O(waiters) enqueue in PRIORITY_FIFO wait order
 * @owns     none
 * @config   CONFIG_EMB_MUTEX
 * @req      KRN-SYNC-001 KRN-SYNC-003 KRN-SYNC-009
 * @since    1.0
 * @stable   yes
 *
 * Return: EMB_OK on acquisition; EMB_ETIMEDOUT; EMB_ECANCELED; EMB_EDESTROYED;
 *         EMB_EOWNERDEAD when configured; EMB_EPERM from a disallowed context
 *         (release builds); EMB_EINVAL or EMB_ESTALE for a bad handle.
 */
emb_status_t emb_mutex_lock(emb_mutex_t mutex, emb_timeout_t timeout);
```

Tag grammar (one tag per line, order free, every tag mandatory unless marked optional):

| Tag | Values | Meaning |
|---|---|---|
| `@ctx` | subset of `thread isr prekernel` | Contexts the function may be called from (§5.2) |
| `@blocks` | `no` or `timeout` | `timeout`: may block, bounded by the trailing timeout or deadline parameter; `no`: never blocks |
| `@time` | `O(1)`, `O(log N)`, `O(N)` with `N` one of `priorities`, `waiters`, `timeouts`, `items`, `len`, `regions`, `caps`; free text after the bound | Timing class per configuration; a function with more than one phase lists each |
| `@owns` | `none`, `takes:<param>`, `gives:<param>`, `borrows:<param>` | Ownership transfer of a buffer or object across the call |
| `@config` | zero or more `CONFIG_EMB_*` | Symbols that must be enabled for the function to exist |
| `@req` | one or more requirement identifiers | Traceability (05 §6) |
| `@since` | version | First release with this signature |
| `@stable` | `yes`, `experimental`, `deprecated:<version>` | Stability promise (§11) |
| `@isr-note` (optional) | free text | What differs when called from an ISR, for `thread isr` functions |

A header whose function lacks a mandatory tag fails the documentation gate (05 §5). The JSON form is one object per function with the tag names as keys and parsed values; its schema lives in `tools/apidoc/schema.json` once the tool exists.

## 10. Compiler portability layer

Generic code uses only these macros for compiler-specific behavior; they are defined per compiler in `include/emb/compiler/<name>.h` and nowhere else (BLD-002, PORT-ABI-002):

```
EMB_INLINE  EMB_ALWAYS_INLINE  EMB_NOINLINE  EMB_NORETURN  EMB_UNUSED  EMB_USED  EMB_WEAK
EMB_ALIAS(sym)  EMB_SECTION(name)  EMB_ALIGNED(n)  EMB_PACKED  EMB_NAKED  EMB_DEPRECATED(msg)
EMB_PRINTF_LIKE(fmt_idx, args_idx)  EMB_LIKELY(x)  EMB_UNLIKELY(x)  EMB_UNREACHABLE()
EMB_COMPILER_BARRIER()  EMB_STATIC_ASSERT(cond, msg)  EMB_CONTAINER_OF(ptr, type, member)
EMB_CLZ32(x)  EMB_CTZ32(x)  EMB_POPCOUNT32(x)   (portable fallbacks where no builtin exists)
EMB_COMPILER_NAME  EMB_COMPILER_VERSION
```

Each macro has a defined fallback (possibly empty) so a new compiler can start with the generic header. Attributes refused by a compiler map to nothing with a documented consequence (for example EmbCC refuses `aligned` on typedefs, so `EMB_ALIGNED` is applied to objects only, which the header rules in §8 already require).

## 11. Versioning and stability

```c
#define EMB_VERSION_MAJOR 0
#define EMB_VERSION_MINOR 2
#define EMB_VERSION_PATCH 0
#define EMB_VERSION       ((EMB_VERSION_MAJOR << 16) | (EMB_VERSION_MINOR << 8) | EMB_VERSION_PATCH)
#define EMB_API_VERSION   1      /* increments on any compatible addition to the public API */
const char *emb_version_string(void);
```

- From 1.0, every `@stable yes` function keeps its signature and documented semantics for all `1.x` releases (05 §7.1). Additions bump `EMB_API_VERSION`.
- Deprecation: the declaration gains `EMB_DEPRECATED("use emb_x_y")`, the annotation becomes `@stable deprecated:<version>`, and the function stays for at least one minor release.
- `experimental` functions live in headers under `include/emb/experimental/` and carry no promise.
- Binary ABI stability is promised only at the syscall boundary of isolated profiles and for the versioned data formats (trace, crash record, debug descriptor). Source compatibility is the public promise.

## 12. Callbacks and entry points

```c
typedef void (*emb_thread_entry_t)(void *arg);    /* returning terminates the thread, KRN-THR-005 */
typedef void (*emb_work_fn_t)(emb_work_t work);   /* thread context on the owning work queue */
typedef void (*emb_timer_fn_t)(emb_timer_t timer, void *arg);  /* work queue by default; ISR only with EMB_TIMER_ISR_CONTEXT and an ISR-safe body */
typedef void (*emb_fault_hook_t)(const emb_fault_info_t *info);  /* may run in any context; must be ISR-safe */
```

A callback's documentation states its context exactly as a function's `@ctx` does. No public API takes a variadic function or is variadic itself; the logging macros are the only variadic surface and they expand to deferred-format calls (ADR-009).

## 13. Worked example

```c
#include <emb/emb.h>

static emb_mutex_storage_t  g_lock_storage;
static emb_mutex_t          g_lock;
static emb_thread_storage_t g_worker_storage;
static uint32_t             g_worker_stack[256 / sizeof(uint32_t)];

static void worker(void *arg)
{
    (void)arg;
    emb_instant_t next = emb_time_now();
    for (;;) {
        next = emb_instant_add(next, EMB_MS(10));
        if (emb_mutex_lock(g_lock, EMB_MS(2)) == EMB_OK) {
            /* ... */
            (void)emb_mutex_unlock(g_lock);
        }
        (void)emb_thread_sleep_until(next);      /* no drift */
    }
}

int main(void)
{
    emb_thread_t  t;
    emb_thread_attr_t ta;

    emb_kernel_init();
    EMB_CHECK(emb_mutex_init(&g_lock_storage, NULL, &g_lock));

    emb_thread_attr_default(&ta);
    ta.name     = "worker";
    ta.priority = 5;
    ta.stack    = g_worker_stack;
    ta.stack_size = sizeof g_worker_stack;
    EMB_CHECK(emb_thread_init(&g_worker_storage, &ta, worker, NULL, &t));
    EMB_CHECK(emb_thread_start(t));

    emb_kernel_start();     /* does not return */
}
```

`EMB_CHECK(expr)` is a checked-build helper that faults on a non-`EMB_OK` status and is the expression's value in release builds.

## 14. Open points for review

1. `emb_status_t` as `int` versus a fixed `int32_t`: `int` chosen for the natural return register and AVR economy; confirm.
2. Structs for the three time types: type safety chosen over scalar typedefs despite EmbCC's inliner limit; confirm, or choose scalar typedefs with analyzer-enforced distinctness.
3. `EMB_ETIMEDOUT` for a `NO_WAIT` miss, instead of a separate would-block code: one code for one outcome; confirm.
4. Whether `emb_status_name()` is in the base profile by default (costs a string table) or opt-in.
