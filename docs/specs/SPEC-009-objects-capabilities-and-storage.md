# SPEC-009 - Kernel Objects, Capabilities, and Storage Generation

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §12. Specification work item 8 of the roadmap (07 §3).
**Requirements:** `docs/requirements/KRN-OBJ.md` (KRN-OBJ-001 to 005 restated; new from 006) and `docs/requirements/KRN-CAP.md` (KRN-CAP-001 to 005 restated; new from 006).
**Builds on:** 03 §7 (object model, capabilities, storage, lifecycle), §8.1 (syscall boundary); SPEC-001 §5.3 (misuse), §6 (handles, storage, attributes, `_DEFINE`); SPEC-004 §6.7 (flush), SPEC-006 §3 (binding slot), SPEC-008 §8 (thread destroy); 02 §7 (profiles); ADR-006, ADR-011, ADR-014, ADR-020, ADR-032, ADR-033, ADR-035; 09 §7 (bracketed sections in `embld` and GNU ld; no linker scripts on AVR).
**Research:** R-001 §7 and §9: FreeRTOS's MPU wrappers look handles up in an O(N) pool; Zephyr builds its object table from DWARF with gperf and keeps per-thread permission bitmaps; Hubris tags task ids with a generation and delivers dead codes; ThreadX and uC/OS-III make the public struct layout the ABI; FreeRTOS mirrors every struct in a dummy static type with a sizeof assert. This specification keeps the handle one word in every profile, puts the generation where it can be checked in O(1), and generates storage sizes from the real layout instead of mirroring or exposing it.

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Object** | A kernel resource with a header (§2): thread, mutex, semaphore, event group, condition variable, barrier, message queue, pipe, pool, timer, work queue, partition, port |
| **Handle** | The one-word public reference (`EMB_DECLARE_HANDLE`, SPEC-001 §6.1) |
| **Capability** | A handle together with the rights it carries; in the pointer models the rights are implicit and total, in the table model they are explicit per entry |
| **Rights** | Object-type-specific bits (§4.3) plus the universal `DESTROY` and `GRANT` |
| **Generation** | A counter bumped when an object is destroyed, re-initialized, or its owning partition restarts; stale references fail with `EMB_ESTALE` where generations are checked |
| **Storage type** | The generated `emb_<obj>_storage_t` with exact size and alignment (ADR-006) |
| **Init table** | The bracketed section that `_DEFINE` macros populate so static objects are constructed before `main()` |
| **Freeze** | `emb_system_freeze()`: no object creation or destruction afterwards (KRN-OBJ-004) |

## 2. Object header

Every object begins with the header; the rest of the layout is private to the kernel.

```c
typedef struct embk_obj {
    uint8_t        type_tag;      /* EMBK_OBJ_THREAD .. EMBK_OBJ_PORT; present in checked builds and isolated profiles */
    uint8_t        lifecycle;     /* EMBK_UNINIT | EMBK_ACTIVE | EMBK_DESTROYING */
    uint8_t        flags;         /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO | type-specific */
    embk_gen_t     generation;    /* CONFIG_EMB_OBJ_GENERATION: uint8_t or uint16_t; absent in tiny */
    const char    *name;          /* CONFIG_EMB_OBJ_NAMES; kept by pointer */
    embk_list_node_t registry;    /* CONFIG_EMB_OBJ_LIST: per-type list for debuggers and statistics */
    embk_binding_t binding;       /* only in types with a ready state (SPEC-006 §3) */
} embk_obj_t;
```

Fields compile out with their options; the tiny profile's header is one byte (`lifecycle`) in release builds and two in checked builds. `lifecycle` is the field every operation checks first after the handle itself: `EMBK_UNINIT` is `EMB_EINVAL` (misuse), `EMBK_DESTROYING` is `EMB_ESTATE`.

## 3. Handle models

The public type is the same everywhere (`struct { uintptr_t raw; }`); what `raw` holds depends on `CONFIG_EMB_HANDLE_MODEL`, chosen by the profile (KRN-CAP-001):

| Model | Profiles | `raw` | Validation on use | Stale detection |
|---|---|---|---|---|
| `POINTER` | tiny, base default | the object pointer | checked builds: `type_tag` and `lifecycle`; release: none | none (undefined behavior on a destroyed object, as in every pointer-based kernel) |
| `INDEXED` | base option | 16-bit slot in the global object registry plus 16-bit slot generation | slot range, slot generation, type, lifecycle: O(1) | yes: `EMB_ESTALE` |
| `TABLE` | isolated, multicore, native | 16-bit index into the calling partition's capability table plus 16-bit slot generation | index range, slot generation, type, **rights**, object generation: O(1), performed at the syscall boundary for unprivileged callers and inline for privileged ones | yes |

Why no tagged pointer: the base-profile idea of "pointer plus generation bits" (03 §7.2) has no portable home. Alignment frees two or three low bits, too few to be useful, and the high byte is not free on Cortex-M address maps (flash at `0x08`, SRAM at `0x20`, peripherals at `0x40`). `INDEXED` gives a real 16-bit generation for one array load and is the model `TABLE` already needs; `POINTER` stays the zero-cost default (§12.1).

### 3.1 `POINTER`

`raw` is the pointer. In checked builds every entry verifies `type_tag` against the expected type (`EMB_FAULT_API_HANDLE`) and `lifecycle == ACTIVE`. Nothing else costs anything (KRN-CAP-005).

### 3.2 `INDEXED`

One global registry `embk_obj_registry[CONFIG_EMB_OBJ_MAX]` of `{object pointer, slot generation}` (four or eight bytes per slot). `init` claims a free slot from an intrusive free list in O(1) and writes the pointer; `destroy` bumps the slot generation and frees the slot. A handle is `slot | (slot_gen << 16)`; use checks `slot < MAX`, `registry[slot].gen == slot_gen`, the object's `type_tag`, and `lifecycle`. The registry lives in kernel RAM; its size is a visible configuration number. Objects created by `_DEFINE` take slots at construction, before `main()`.

### 3.3 `TABLE` (capabilities)

Each partition owns a capability table of `CONFIG`-sized length emitted by the generator (KRN-CAP-004):

```c
typedef struct embk_cap {
    embk_obj_t *object;        /* NULL = free slot */
    embk_gen_t  object_gen;    /* the object's generation when the capability was created or granted */
    uint16_t    slot_gen;      /* bumped when the slot is freed */
    uint8_t     type_tag;
    uint8_t     rights;        /* §4.3 */
} embk_cap_t;
```

A handle is `index | (slot_gen << 16)`. Validation (`embk_cap_lookup(partition, handle, type, needed_rights)`) checks index range, `slot_gen`, `type_tag`, `rights ⊇ needed`, `object->lifecycle == ACTIVE`, and `object->generation == object_gen` (a destroyed and re-initialized object, or a restarted partition's object, fails here with `EMB_ESTALE`). For unprivileged callers the check happens at the syscall boundary (03 §8.1, KRN-CAP-002); for privileged callers it is the same inline function, so privileged code gets the same rights discipline at the cost of one lookup. The kernel partition's own table is the one the kernel uses for its threads.

## 4. Capabilities

### 4.1 Where capabilities come from

- **Build time.** The system description (SPEC-014) declares objects and which partitions hold which rights to them; the generator emits each partition's initial table (`partitions.c`, 04 §1.3). This is how a driver partition receives its device and port capabilities and how an application partition receives its queues.
- **Creation.** `init` by a partition places a full-rights capability in that partition's table (minus `GRANT` if the partition's own capability to itself lacks `GRANT_TO`).
- **Grant.** `emb_cap_grant(emb_partition_t to, emb_handle_t cap, uint8_t rights, emb_handle_t *out)` copies an entry into another partition's table with `rights ⊆ own rights`; requires `GRANT` on `cap` and `GRANT_TO` on the partition capability for `to`. Returns the new index in the target's table, which the granter then communicates through a port message or a shared configuration (FUTURE: ports carry capabilities in messages).
- **Derive.** `emb_cap_derive(cap, rights, *out)`: a second entry in the caller's own table with fewer rights, for handing to a less trusted component in the same partition (KRN-CAP-003). Keeping `GRANT` in a derived capability requires `GRANT`.
- **Delete.** `emb_cap_delete(cap)` frees the caller's entry (slot generation bump).
- **Revoke.** `emb_cap_revoke(cap)` by a holder with `DESTROY` bumps the **object's** generation: every capability to that object everywhere fails from then on (`EMB_ESTALE`), including the revoker's own; the object stays alive and the owner re-creates a fresh capability with `emb_cap_renew(cap_with_DESTROY)`. Revocation is coarse by design (03 §7.2): per-entry revocation would need back-pointers from objects to tables.

### 4.2 Rights

| Object | Type-specific rights | Universal |
|---|---|---|
| thread | `START`, `SUSPEND`, `CANCEL`, `PRIORITY`, `JOIN`, `NOTIFY` | `DESTROY`, `GRANT` |
| mutex | `LOCK` | |
| semaphore | `TAKE`, `GIVE`, `BIND` | |
| event group | `WAIT`, `SET`, `CLEAR`, `BIND` | |
| condition variable | `WAIT`, `SIGNAL` | |
| barrier | `WAIT` | |
| message queue | `SEND`, `RECEIVE`, `BIND` | |
| pipe | `READ`, `WRITE`, `CLOSE`, `BIND` | |
| pool | `ALLOC`, `FREE` | |
| timer | `START`, `STOP` | |
| work queue | `SUBMIT`, `FLUSH` | |
| partition | `RESTART`, `SUSPEND`, `GRANT_TO`, `OBSERVE` | |
| port (FUTURE) | `SEND`, `RECEIVE`, `BIND` | |

Rights fit in eight bits per type; `DESTROY` and `GRANT` occupy the top two bits of every type's mask. Each operation's `@req` annotation (SPEC-001 §9) names the right it needs, and the generated syscall stubs derive their checks from it (SPEC-010).

### 4.3 In the pointer models

With `POINTER` and `INDEXED` there are no rights: every holder has all of them. `emb_cap_*` functions exist, return `EMB_OK` where the semantics are trivial (`derive` returns the same handle, `delete` is a no-op) and `EMB_ENOTSUP` where they are not (`grant`, `revoke`), so that code written for the isolated profile compiles and runs on the others (KRN-CAP-001).

## 5. Storage generation (ADR-006)

### 5.1 Mechanism

1. The build compiles `kernel/layout/probe.c` **for the target** with the final configuration; the file defines, for every object type, `const char embk_size_<type>[sizeof(struct embk_<type>)]` and an alignment probe.
2. `tools/gen_storage` reads the symbol sizes from the probe object with the toolchain's object reader (`nm`, `embnm`) and emits `<emb/storage.h>`: `EMB_<OBJ>_STORAGE_SIZE`, `EMB_<OBJ>_STORAGE_ALIGN`, and the `emb_<obj>_storage_t` unions of SPEC-001 §6.2.
3. Every kernel source file that defines an object type contains `_Static_assert(sizeof(struct embk_<type>) <= EMB_<OBJ>_STORAGE_SIZE && _Alignof(...) <= EMB_<OBJ>_STORAGE_ALIGN)`, so a stale header cannot link a wrong size.
4. The configuration hash is embedded in the header; a mismatch between the hash in `<emb/storage.h>` and the kernel's configuration is a build error (BLD-007).

Two passes over the compiler are the price; CMake handles them as one target with a dependency. Sizes are reproducible from configuration and toolchain (BLD-004 reproducibility).

### 5.2 `_DEFINE` and the init table

`EMB_<OBJ>_DEFINE(name, attr...)` expands to the storage, the handle variable, a constant attribute struct, and an entry `{init_fn, &storage, &attr, &handle}` in the bracketed section `.emb_init_table`, which `embld` and GNU ld both provide on every target (09 §7; no linker script is needed on AVR). Kernel start walks the table in link order and calls each `init` before `main()`; thread entries are also started at `emb_kernel_start()` unless marked `EMB_THREAD_NO_AUTOSTART` (SPEC-008 §4). Entries from different translation units have no defined relative order; an object that depends on another at init time (a timer on a work queue) must be initialized in code or by the `EMB_INIT_AFTER(other)` attribute, which the generator honours by ordering the table it emits for system-described objects.

### 5.3 Objects in flash

Objects whose state never changes could live in flash; 1.0 does not support it (§12.5): kernel objects carry mutable headers, and a const-initializer form would expose the layout the storage type exists to hide. The init-table mechanism costs one function call per object at boot and keeps the layout private.

## 6. Lifecycle

```
UNINIT --init--> ACTIVE --destroy (no waiters, or ABORT_WAITERS: flush)--> DESTROYING --> UNINIT (generation + 1)
```

- `init` on storage whose header reads `ACTIVE` is misuse in checked builds (`EMB_FAULT_API_LIFECYCLE`: double init) and `EMB_EEXIST` in release builds; storage must be zero or previously destroyed. `_DEFINE` storage is zero-initialized by the C runtime.
- `destroy` follows ADR-020 and KRN-OBJ-001: with waiters and without `ABORT_WAITERS`, misuse (`EMB_EBUSY`); with it, every waiter wakes with `EMB_EDESTROYED` (SPEC-004 §6.7); type-specific rules apply (an owned mutex, a pool with outstanding blocks, a timer with a pending callback, a work queue with items, a running thread: all `EMB_EBUSY`). The object passes through `DESTROYING` so that a concurrent operation on another CPU observes `EMB_ESTATE` instead of racing, then becomes `UNINIT` with its generation bumped (KRN-OBJ-002) and its registry or table slots released.
- **Freeze.** `emb_system_freeze()` (KRN-OBJ-004) sets a kernel flag; afterwards every `init`, `destroy`, `cap_grant`, `cap_derive`, `cap_delete`, and `cap_revoke` returns `EMB_EPERM` (fault in checked builds). Partition restart re-initializes a partition's objects through the kernel's own path, which the flag does not block. The isolated reference configuration calls it at the end of initialization; the `safety` profile attribute (ADR-035) requires it.
- **Partition restart** (SPEC-010) bumps the generation of every object the partition owns and of the partition itself, re-initializes their storage from the image, and rebuilds the partition's capability table from the generated initial table; other partitions' capabilities to the restarted partition's objects fail with `EMB_ESTALE` until re-granted, and waiters on them are flushed with `STALE` (ADR-032, KRN-OBJ-005).

## 7. Names, registry, and tooling

- `CONFIG_EMB_OBJ_NAMES` keeps the `name` pointer from the attribute; `emb_obj_name(handle)` returns it. Names are for debugging and trace; the kernel never compares them.
- `CONFIG_EMB_OBJ_LIST` links every active object into a per-type list; `emb_obj_foreach(type, cb, arg)` iterates under the scheduler lock. The debug descriptor (ADR-011) exports the list heads, the header offsets, the type tags, the registry (INDEXED), and the capability table layout (TABLE), so a debugger can list every object, its waiters, and its holders.
- Type tags are small integers, not ASCII words: the descriptor maps them to names.

## 8. Misuse and checked-build diagnostics

| Condition | Checked build | Release build |
|---|---|---|
| null handle; wrong type tag (POINTER); bad index, slot generation, or type (INDEXED, TABLE) | fault `EMB_FAULT_API_HANDLE` | `EMB_EINVAL` |
| object generation mismatch (INDEXED, TABLE) | runtime condition | `EMB_ESTALE` |
| missing right (TABLE) | fault `EMB_FAULT_API_HANDLE` for privileged callers | `EMB_EPERM` (always a status for unprivileged callers, KRN-CAP-002) |
| `init` on active storage | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EEXIST` |
| `destroy` with waiters, owner, outstanding blocks, pending callback, items, or a running thread | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EBUSY` |
| operation on a `DESTROYING` object | runtime condition | `EMB_ESTATE` |
| `init`, `destroy`, or capability change after freeze | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EPERM` |
| misaligned or undersized storage | fault `EMB_FAULT_API_ARGUMENT` | `EMB_EINVAL` |
| registry or capability table exhausted | runtime condition | `EMB_ENOMEM` |
| `grant` or `derive` asking for rights the caller lacks | fault `EMB_FAULT_API_HANDLE` | `EMB_EPERM` |

## 9. Observability

Trace events: `obj_init(type, handle, name)`, `obj_destroy(type, handle, waiters_flushed)`, `cap_grant(from, to, handle, rights)`, `cap_derive`, `cap_delete`, `cap_revoke(handle)`, `system_freeze`. Statistics: registry and capability table occupancy high-water, stale-handle rejections per partition (a security signal).

## 10. Profiles

| Profile | Handle model | Generation | Names | Lists | Freeze |
|---|---|---|---|---|---|
| tiny | POINTER | none | off | off | available |
| base | POINTER (INDEXED optional) | 16-bit with INDEXED | on | on | available |
| isolated, multicore | TABLE | 16-bit | on | on | required by the `safety` attribute |
| native | TABLE | 16-bit | on | on | available |

## 11. Reference model

Capability checks are static validation with no concurrency; they are tested by the conformance suite, not modeled. Generations and `EMB_ESTALE` appear in the model where they interact with waits (destroy flush, join on a restarted thread) and are extended with partition restart in SPEC-010.

## 12. Decisions taken at acceptance (2026-10-07)

1. Three handle models behind one type: `POINTER` (tiny, base default), `INDEXED` (base option: global registry, 16-bit slot generation), `TABLE` (isolated, multicore, native: per-partition capability tables). No tagged pointers.
2. Rights are eight bits per type with `DESTROY` and `GRANT` universal; in the pointer models the capability functions exist and are trivial or `EMB_ENOTSUP`.
3. Revocation is by object generation (coarse); `renew` re-creates the owner's capability.
4. Storage sizes are generated from a target-compiled layout probe read with the toolchain's object reader, checked by static assertions, and stamped with the configuration hash.
5. Objects live in RAM and are constructed from the bracketed init table before `main()`; flash-resident objects are not supported in 1.0.
6. Double `init` is `EMB_EEXIST`; destroy passes through `DESTROYING`; freeze blocks every create, destroy, and capability change but not partition restart.
7. Type tags are small integers mapped to names by the debug descriptor.
