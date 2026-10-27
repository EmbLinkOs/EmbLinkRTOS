# M2-02: Object lifecycle and storage reuse

This closes the last open M2-02 item of the [checklist](m2-checklist.md): review the current POINTER-handle and lifecycle error behavior and the stated costs against the code, and correct what disagrees. The [contract review](m2-02-contract-review.md) recorded the gap: thread, semaphore and mutex init reset whatever storage they were given. Thread init was corrected separately (SPEC-008 §12). This change corrects semaphores and mutexes under ADR-045 and amends KRN-OBJ-007.

## Defect

`emb_sem_init()` and `emb_mutex_init()` on storage that still held an active object succeeded and overwrote it. A blocked waiter stayed enqueued in a queue that no longer listed it. A mutex owner lost ownership while its owned list still linked the mutex. A semaphore binding vanished. Nothing was reported in either build. The new conformance cases demonstrate it on the previous kernel (source `3bd0cf7`, cases forced on): checked builds report no fault, release builds return `EMB_OK` with a new handle, and the mutex case observes that the original owner no longer owns its mutex.

## Decision

`CONFIG_EMB_OBJ_LIST` (KRN-OBJ-011) keeps every active semaphore and mutex on a per-type list (`embk_sem_all`, `embk_mutex_all`) from init to destroy, under the scheduler lock. Init compares the storage's list node with the members before touching anything, then refuses a match with `EMB_FAULT_API_LIFECYCLE` (checked) or `EMB_EBUSY` (release), argument 1. The comparison is by address, so uninitialized storage is never mistaken for a live object, whatever bytes it holds. The lists are statically initialized, so construction before `emb_kernel_init()` keeps working. The option defaults on, except for the tiny profile, whose flash budget has no room for it; there, double init of a semaphore or mutex stays undefined and is documented as such.

`EMB_EBUSY` replaces KRN-OBJ-007's `EMB_EEXIST` for every type, matching the thread refusal already shipped and the in-use destroy refusals of KRN-OBJ-010. ADR-045 records the alternatives: header plausibility checks, mandatory zeroed storage, and per-object generations.

## Lifecycle review

| Object | Init refuses | Destroy refuses (checked fault / release status) | Storage reusable |
|---|---|---|---|
| Thread | storage holding an undestroyed thread (exact with `CONFIG_EMB_THREAD_LIST`, else tag + state + reserved ID); tiny priority slot taken (`EMB_EBUSY`); IDs exhausted (`EMB_ENOMEM`) | started and not (terminated, joined or detached, switched out): `EMB_EBUSY`; kernel thread: `EMB_EPERM` | after destroy |
| Semaphore | storage holding an active semaphore, with `CONFIG_EMB_OBJ_LIST` | waiters without `EMB_OBJ_ABORT_WAITERS`: `EMB_EBUSY` | after destroy |
| Mutex | storage holding an active mutex, with `CONFIG_EMB_OBJ_LIST` | owned by another thread, or waiters without `EMB_OBJ_ABORT_WAITERS`: `EMB_EBUSY` | after destroy; a mutex the caller owns is released by destroy |

Argument validation precedes the storage check in all three init paths. The refusal leaves the output handle cleared, as for every init failure. A refused destroy changes nothing.

POINTER handles carry no generation (SPEC-009 §3.1). After destroy, a retained copy fails the active-state check only while the storage stays destroyed; after re-init, the copy refers to the new object. Handles to storage that is no longer accessible are not detected. Neither case is promised to return `EMB_EINVAL`. Storage lifetime and invalidation of copies remain caller obligations. Semaphore bindings still have no teardown, which is M2-03.

## Cost review

| Function | Annotation before | Code | Annotation now |
|---|---|---|---|
| `emb_thread_init` | bounded ID scan + stack fill | also walks the all-threads list | adds O(live threads) with `CONFIG_EMB_THREAD_LIST` |
| `emb_sem_init` | O(1) | O(1); O(active semaphores) with the list | O(1); O(active semaphores) with `CONFIG_EMB_OBJ_LIST` |
| `emb_mutex_init` | O(1) | O(1); O(active mutexes) with the list | O(1); O(active mutexes) with `CONFIG_EMB_OBJ_LIST` |
| `emb_sem_destroy`, `emb_mutex_destroy` | unchanged | list removal is O(1) | unchanged |

The list walk runs with interrupts masked. Init is not a real-time path: its contexts are prekernel and thread. An application that creates many objects of one type at run time pays the walk once per init. The inheritance depth bound, the deadlock-detection depth bound and the full resource-bound inventory stay with the contract review and M2-04.

Generated storage, `EMB_*_STORAGE_SIZE` in bytes, before and after:

| Configuration | Semaphore | Mutex | Thread |
|---|---|---|---|
| native base (64-bit, 16-byte storage alignment) | 64 → 80 | 64 → 80 | 352 (unchanged) |
| native tiny | 48 (unchanged) | 48 (unchanged) | 256 (unchanged) |
| Cortex-M4F base (`cm4-an386`) | 36 → 44 | 32 → 40 | 120 (unchanged) |
| Cortex-M4F tiny | 24 (unchanged) | 24 (unchanged) | 64 (unchanged) |

On `cm4-an386`, the kernel archive grows from 11,208 to 11,432 bytes of text (semaphore +116, mutex +116, thread −8 from the shared list helper) and gains 16 bytes of data for the two list heads. The tiny archive is byte-for-byte unchanged in text, data and bss, so the AVR tiny footprint gates should be unaffected; they run in CI.

## Verification

Source `ae6f1b1` on macOS 27.2 (arm64), CMake 4.4.3, Ninja 1.13.2, QEMU 11.1.1; native with Apple clang 21.0.0 (`gcc`) and Homebrew clang 23.1.0, Cortex-M with Arm GNU Toolchain 14.2.Rel1.

| Configuration | CTest targets | New cases |
|---|---|---|
| native-gcc, native-clang, native-periodic (checked) | 15/15 | both pass; the refusal is observed as a fault in a forked child |
| native-release | 15/15 | both pass; `EMB_EBUSY`, cleared output, live object intact |
| native-tiny | 15/15 | compiled out (no list) |
| cm3-an385, cm4-an386 (checked, QEMU) | 12/12, 13/13 | pass with the fault assertion skipped (no `fork` on target) |
| cm4-an386-release (QEMU) | 13/13 | both pass; refusal observed |
| cm4-an386-tiny (QEMU) | 13/13 | compiled out |

Each case blocks a waiter on the live object (and, for the mutex, owns it), attempts re-init, checks the refusal and the cleared output in release, then checks that the waiter is still served and the owner still owns the mutex, and finally reuses the storage after destroy. Four existing tests left objects alive at their end and now destroy them.

The strict documentation build could only be approximated locally, with Doxygen 1.18.0 standing in for the pinned 1.15.0: it reports the same seven warnings in generated API pages for unchanged headers on `main` (`3bd0cf7`) as on this change, so the documentation job's pinned toolchain is authoritative here.

Later on the same machine, with the editor-setup branch's test fix merged in: RV32 under QEMU (rv32-virt, -release, -tiny: 12/12 each; Homebrew riscv64-elf-gcc 16.2 with a locally built picolibc 1.8.10), AVR under QEMU (avr-uno, avr-uno-release: 12/12; avr-uno-t1: 11/11; Homebrew avr-gcc 14.4.0) and the EmbSIM board suites (avr-uno-embsim 12/12, cm4-nucleo-embsim 13/13, rv32-c3-embsim 12/12, each with EmbSIM built at the reference CI pins). On avr-uno-t1 the kernel archive's instructions and per-object sizes are identical to `main`'s built with the same compiler: kernel RAM 64 bytes and TCB 32 bytes on both. Its kernel text, 4,126 bytes for both, exceeds the 4,096-byte gate only because avr-gcc 14.4 generates larger code than the gcc-avr 7.3 the gate is defined with; CI measures the gate. No hardware is involved. The SMP `DESTROYING` state of KRN-OBJ-007 remains unimplemented on uniprocessor builds.
