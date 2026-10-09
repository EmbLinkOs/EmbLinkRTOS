# KRN-IPC - Inter-Thread Communication

Group `KRN-IPC`. Design: `docs/specs/SPEC-007-inter-thread-communication.md`. Related groups: KRN-WAIT (protocol), KRN-NOTIF (binding), KRN-MEM (pools and storage), KRN-CAP (rights on queues and ports), KRN-PART (ports and leases).

KRN-IPC-001 to 005 originate in the v0.1 specification (§9.4); KRN-IPC-006 to 009 in `docs/architecture/03-kernel-architecture.md` §6.5, §6.6 (009 added by ADR-032). All are restated here as the authoritative copy. New requirements start at 010.

---

### KRN-IPC-001  Common wait mechanism
**Statement.** IPC primitives that block shall use the common wait mechanism.
**Rationale.** One protocol (SPEC-004) under every primitive.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: every blocking entry in `kernel/ipc/` uses `embk_wait_prepare`/`embk_wait_commit` or `embk_block_on`.
**Trace.** SPEC-007 §1; SPEC-004; v0.1 §9.4

### KRN-IPC-002  Determinable capacity and memory
**Statement.** Queue capacity and memory use shall be determinable from configuration and application allocation.
**Rationale.** No hidden allocation on an MCU.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `EMB_MSGQ_RING`, `EMB_POOL_STORAGE`, and the pipe buffer are caller-provided; the storage generator reports object sizes.
**Trace.** SPEC-007 §2.1, §3.1, §4.1; 03 §9.2

### KRN-IPC-003  ISR-safe IPC never blocks
**Statement.** ISR-safe IPC operations shall never block.
**Rationale.** SPEC-001 §5.2.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `@ctx thread isr` forms accept only `EMB_NO_WAIT`; Test: `misuse/ipc_block_from_isr`.
**Trace.** SPEC-007 §1, §7

### KRN-IPC-004  Explicit zero-copy ownership
**Statement.** Zero-copy APIs, if provided, shall define ownership and lifetime rules explicitly.
**Rationale.** Lifetime hazards are the price of zero copy (v0.1 §9.3).
**Status.** Accepted 2026-10-07
**Verification.** Analysis: SPEC-007 §4.3 rules; Test: checked-build `misuse/pool_free_non_owner`.
**Trace.** SPEC-007 §4.3

### KRN-IPC-005  Deterministic wake order
**Statement.** Wake ordering for multiple waiters shall be deterministic and documented.
**Rationale.** Analyzability.
**Status.** Accepted 2026-10-07
**Verification.** Test: `wait/order_*` against queues, pipes, and pools.
**Trace.** SPEC-007 §2.2, §3.2, §4.2; KRN-WAIT-001, 002

### KRN-IPC-006  Ownership moves with the message
**Statement.** Zero-copy transfer shall move ownership explicitly; a transferred block shall not be accessed by the sender after transfer.
**Rationale.** 03 §6.5.
**Status.** Accepted 2026-10-07
**Verification.** Test: checked-build `misuse/pool_use_after_send` faults; release-build documentation review.
**Trace.** SPEC-007 §4.3

### KRN-IPC-007  Bounded pools, O(1) allocation
**Statement.** Buffer pools shall be bounded and allocation shall be O(1).
**Rationale.** Real-time paths.
**Status.** Accepted 2026-10-07
**Verification.** Benchmark: pool alloc and free in the harness; Analysis: intrusive free list.
**Trace.** SPEC-007 §4.2

### KRN-IPC-008  Ports share queue semantics
**Statement.** Cross-partition and cross-core messaging shall use the same handle, timeout, and wake semantics as intra-image queues.
**Rationale.** One mental model from a single image to a multi-core, multi-partition system.
**Status.** Accepted 2026-10-07 (FUTURE feature; the constraint binds the queue API now)
**Verification.** Analysis at port specification time: the port API is a superset of the queue API.
**Trace.** SPEC-007 §5; 03 §6.6

### KRN-IPC-009  Leases for large cross-partition buffers
**Statement.** Cross-partition buffers larger than the configured copy threshold shall be transferred by lease, validated on every access, and revoked by the client's wake.
**Rationale.** ADR-032.
**Status.** Accepted 2026-10-07 (FUTURE feature)
**Verification.** Deferred to the port specification.
**Trace.** SPEC-007 §5; ADR-032

### KRN-IPC-010  Message queue semantics
**Statement.** A message queue shall copy fixed-size items by value into a caller-provided ring; send with a waiting receiver shall copy directly into the receiver and wake it; receive that frees a slot shall move a waiting sender's item into it and wake the sender; a woken thread shall not re-check; a full queue shall return `EMB_ETIMEDOUT` to a non-blocking sender and never drop.
**Rationale.** Hand-off semantics (KRN-WAIT-012) applied to both sides; no silent drops (R-001 §6, RIOT).
**Status.** Accepted 2026-10-07
**Verification.** Test: `ipc/msgq_*` including `msgq_direct_to_receiver`, `msgq_slot_handoff_to_sender`, `msgq_full_nowait_timedout`.
**Trace.** SPEC-007 §2.2

### KRN-IPC-011  Bounded item copy under the lock
**Statement.** `item_size` shall be bounded by `CONFIG_EMB_MSGQ_MAX_ITEM` (default 64, tiny 16); the copy shall run inside the object's critical section and its cost shall be included in the masked-time budget.
**Rationale.** Simplicity and bounded masking (R-003 T7) without a reservation protocol.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/msgq_item_too_large`; Benchmark: masked time of a maximum-size send.
**Trace.** SPEC-007 §2.2, §2.3

### KRN-IPC-012  Send to front
**Statement.** `send_front` shall place an item at the head, shall accept only `EMB_NO_WAIT`, and shall be ISR-safe.
**Rationale.** Urgent messages without a second queue (03 §6.3).
**Status.** Accepted 2026-10-07
**Verification.** Test: `ipc/msgq_send_front_order`.
**Trace.** SPEC-007 §2.2

### KRN-IPC-013  Pipe semantics
**Statement.** A pipe shall write as much as fits and block for the remainder when the timeout allows, reporting the partial count on timeout; read shall return once `min_len` bytes are available, up to `len`; readers shall be woken with the available count handed over and shall copy in their own context.
**Rationale.** Partial transfers with per-call minimums are what stream consumers need (Zephyr pipe rewrite, R-001 §6).
**Status.** Accepted 2026-10-07
**Verification.** Test: `ipc/pipe_partial_write_timeout`, `ipc/pipe_read_min_len`.
**Trace.** SPEC-007 §3.2

### KRN-IPC-014  Pipe chunking and SPSC path
**Statement.** Pipe copies shall run in chunks of at most `CONFIG_EMB_PIPE_CHUNK` bytes inside the critical section, releasing it between chunks; writes of at most one chunk shall be atomic with respect to other writers; with `EMB_PIPE_SPSC` the data copy shall run without the critical section and publish indices with release semantics.
**Rationale.** Bounded masking for general pipes; a lock-free fast path for the common single-producer single-consumer case (FreeRTOS stream buffers).
**Status.** Accepted 2026-10-07
**Verification.** Test: `ipc/pipe_chunk_atomicity`, `ipc/pipe_spsc_lockfree` (monitor shows no masked copy); checked-build `misuse/pipe_spsc_second_writer`.
**Trace.** SPEC-007 §3.2

### KRN-IPC-015  Pipe close and reset
**Statement.** `close` shall refuse further writes with `EMB_ESTATE`, wake blocked writers with `EMB_ESTATE`, and let readers drain before returning `EMB_ESTATE`; `reset` shall discard contents, wake every waiter with `EMB_ESTATE`, and reopen.
**Rationale.** A defined end-of-stream and a defined restart.
**Status.** Accepted 2026-10-07
**Verification.** Test: `ipc/pipe_close_drain`, `ipc/pipe_reset`.
**Trace.** SPEC-007 §3.2

### KRN-IPC-016  Buffer pool semantics
**Statement.** A pool shall keep an intrusive O(1) free list; `alloc` on an exhausted pool shall block with the wait protocol; `free` with a waiter shall hand the block over; destroy with blocks outstanding shall be misuse.
**Rationale.** 03 §6.5, §9.2.
**Status.** Accepted 2026-10-07
**Verification.** Test: `ipc/pool_alloc_free`, `ipc/pool_handoff`, `misuse/pool_destroy_outstanding`.
**Trace.** SPEC-007 §4.2

### KRN-IPC-017  Ownership tracking in checked builds
**Statement.** Checked builds shall track each block's owner (thread, `IN_QUEUE`, or transferred) and shall fault on free, transfer, or ownership send by a non-owner, on double free, and on a pointer outside the pool; release builds shall track nothing.
**Rationale.** Zero-copy bugs are found in development at no production cost.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/pool_*` in checked builds; footprint test shows no owner field in release builds.
**Trace.** SPEC-007 §4.3, §7

### KRN-IPC-018  Bindings on IPC objects
**Statement.** Message queues (receive side, empty to non-empty), pipes (fill reaching `notify_trigger`), and pools (exhausted to available) shall support the notification binding, and a transition consumed by a hand-off shall not signal it.
**Rationale.** KRN-NOTIF-004.
**Status.** Accepted 2026-10-07
**Verification.** Test: `notif/bind_msgq`, `notif/bind_pipe`, `notif/bind_pool`.
**Trace.** SPEC-007 §2.2, §3.2, §4.2; SPEC-006 §3

### KRN-IPC-019  Storage declaration helpers
**Statement.** `EMB_MSGQ_RING`, `EMB_POOL_STORAGE`, and the pipe buffer rules shall declare correctly sized and aligned caller storage; misaligned or undersized storage shall be misuse.
**Rationale.** Word copies and intrusive free lists need alignment the application should not have to compute.
**Status.** Accepted 2026-10-07
**Verification.** Test: `misuse/ipc_storage_misaligned`; Analysis: macro review.
**Trace.** SPEC-007 §2.1, §4.1, §7

### KRN-IPC-020  Trace and statistics
**Statement.** Send, receive, blocked send, pipe write, read and close, pool alloc, free and transfer shall be trace events; high-water occupancy, blocked-send counts, minimum pool availability and blocks outstanding shall be optional statistics; the debug descriptor shall export ring and free-list layouts.
**Rationale.** Queue sizing and leak hunting need these numbers.
**Status.** Accepted 2026-10-07
**Verification.** Test: trace decoder; statistics API test.
**Trace.** SPEC-007 §8; OBS-005, OBS-008

### KRN-IPC-021  Tiny profile
**Statement.** The tiny profile shall offer message queues and pools with 8-bit counts under `CONFIG_EMB_IPC_SMALL`, a 16-byte default item bound, and pipes only when configured.
**Rationale.** Footprint targets T1 and T2.
**Status.** Accepted 2026-10-07
**Verification.** Footprint test of the tiny reference configuration.
**Trace.** SPEC-007 §9

### KRN-IPC-022  Model coverage
**Statement.** Message queues and pools shall be added to the reference model with their M2 conformance tests, with conservation properties (every item received exactly once or still in the ring; every block owned by one party or free).
**Rationale.** The same evidence standard as semaphores and mutexes.
**Status.** Accepted 2026-10-07
**Verification.** Analysis: `tools/model` scenarios exist before the M2 implementation of queues and pools.
**Trace.** SPEC-007 §10; KRN-WAIT-007
