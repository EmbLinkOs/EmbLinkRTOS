# SPEC-007 - Inter-Thread Communication: Message Queues, Pipes, Buffer Pools, Ports

**Status:** Accepted 2026-10-07 by the project owner ("do all"), with the defaults of §11. Specification work item 6b of the roadmap (07 §3), added because M2 delivers message queues.
**Requirements:** `docs/requirements/KRN-IPC.md` (KRN-IPC-001 to 005 from v0.1 §9.4 and 006 to 009 from 03 §6 restated; new from 010).
**Builds on:** SPEC-004 (wait protocol, hand-off), SPEC-006 §3 (binding), SPEC-001 §5, §6 (contexts, handles, storage), SPEC-005 §4 (the semaphore as the pattern a queue repeats twice), 03 §6.3 to §6.6, §9.2; ADR-020, ADR-027, ADR-032 (leases).
**Research:** R-001 §6: copy-by-value queues with direct copy into a waiting receiver (ThreadX, Zephyr, RTEMS) are the fast common case; FreeRTOS stream buffers show the lock-free single-producer single-consumer byte stream; Zephyr's pipe rewrite shows partial transfers with per-call minimums; zero copy by ownership transfer (buffer pools) is the only zero-copy form that is safe inside one address space, and leases (Hubris) the only one that is safe across partitions.

---

## 1. Scope and terms

| Primitive | 1.0 | Blocking (`thread`) | ISR-safe (`thread isr`, `EMB_NO_WAIT` only) | Section |
|---|---|---|---|---|
| Message queue | yes | `send`, `receive`, `_until` forms | `send`, `send_front`, `receive`, `peek` with `EMB_NO_WAIT` | §2 |
| Pipe (byte stream) | yes, `CONFIG_EMB_PIPE` | `write`, `read`, `_until` forms | `write`, `read` with `EMB_NO_WAIT` | §3 |
| Buffer pool | yes | `alloc`, `alloc_until` | `alloc` with `EMB_NO_WAIT`, `free` | §4 |
| Port | FUTURE | request, reply | none | §5 |

A **message** is a fixed-size item copied by value. A **block** is a fixed-size buffer from a pool whose ownership moves with the message that carries its pointer (zero copy). A **stream** is bytes without message boundaries. Everything is caller-provided storage (KRN-IPC-002, 03 §9.2), every blocking operation is a thin layer over SPEC-004, and every ready transition can signal a binding (SPEC-006 §3).

## 2. Message queue

### 2.1 Interface

```c
typedef struct emb_msgq_attr {
    const char *name;
    void       *storage;        /* ring buffer: capacity * item_size bytes, aligned to EMB_MSGQ_ALIGN */
    uint16_t    item_size;      /* 1 .. CONFIG_EMB_MSGQ_MAX_ITEM (default 64) */
    uint16_t    capacity;       /* >= 1 */
    uint8_t     flags;          /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO | EMB_MSGQ_OWNERSHIP */
    emb_pool_t  pool;           /* EMB_MSGQ_OWNERSHIP: the pool whose blocks travel in this queue */
} emb_msgq_attr_t;

#define EMB_MSGQ_RING(name, item_size, capacity)  /* declares aligned storage of the right size */

emb_status_t emb_msgq_init(emb_msgq_storage_t *storage, const emb_msgq_attr_t *attr, emb_msgq_t *out);  /* @ctx prekernel thread */
emb_status_t emb_msgq_destroy(emb_msgq_t q);
emb_status_t emb_msgq_send(emb_msgq_t q, const void *item, emb_timeout_t timeout);       /* @ctx thread; thread isr with EMB_NO_WAIT  @time O(1) + copy */
emb_status_t emb_msgq_send_until(emb_msgq_t q, const void *item, emb_instant_t deadline);
emb_status_t emb_msgq_send_front(emb_msgq_t q, const void *item);                         /* @ctx thread isr  EMB_NO_WAIT only: urgent, to the head */
emb_status_t emb_msgq_receive(emb_msgq_t q, void *out_item, emb_timeout_t timeout);      /* @ctx thread; thread isr with EMB_NO_WAIT */
emb_status_t emb_msgq_receive_until(emb_msgq_t q, void *out_item, emb_instant_t deadline);
emb_status_t emb_msgq_peek(emb_msgq_t q, void *out_item);                                 /* @ctx thread isr  copies the head without removing it */
uint16_t     emb_msgq_count(emb_msgq_t q);                                                /* @ctx thread isr  snapshot */
uint16_t     emb_msgq_free(emb_msgq_t q);
emb_status_t emb_msgq_bind_notify(emb_msgq_t q, emb_thread_t t, uint8_t bit);            /* receive side: empty -> non-empty */
```

### 2.2 Semantics

- **Storage.** The ring holds `capacity` items of `item_size` bytes; `EMB_MSGQ_RING()` declares it with the alignment the kernel needs for word copies. `item_size` above `CONFIG_EMB_MSGQ_MAX_ITEM` is `EMB_EINVAL`: large payloads travel as block pointers (§4), never as large copies.
- **Two wait queues**: receivers (when empty) and senders (when full), both with the object's wait policy (`PRIORITY_FIFO` default, `EMB_OBJ_FIFO` optional).
- **Send.** If a receiver waits: copy the item directly into the receiver's `out_item` and wake it with hand-off (the ring is untouched; no binding bit, SPEC-004 §6.3). Else if the ring has room: copy into the tail slot; if the ring was empty, signal the binding. Else: block on the senders' queue; the receiver that frees a slot copies this sender's item into the freed slot and wakes it (slot hand-off), so a woken sender never re-checks. The copy happens inside the object's critical section; its cost is bounded by `CONFIG_EMB_MSGQ_MAX_ITEM` and is part of the masked-time budget (R-003 T7).
- **Send to front.** Writes at the head (the next item received) and never blocks (`EMB_ETIMEDOUT` when full); for urgent messages (03 §6.3).
- **Receive.** If the ring is non-empty: copy the head; then, if a sender waits, move its item into the freed tail slot and wake it. Else if a sender waits with the ring empty (capacity reached zero is impossible; this branch is for `capacity` consumed by `send_front` races and is kept for completeness): hand the sender's item directly. Else block on the receivers' queue; the hand-off delivers the item into `out_item` before the wake.
- **Peek** copies the head without removing it; `count` and `free` are snapshots.
- **Ownership queues.** With `EMB_MSGQ_OWNERSHIP`, items are block pointers of `pool`: `send` transfers the block's ownership to the queue and `receive` assigns it to the receiver (§4.3). In checked builds the owner field of the block is checked and updated; release builds copy the pointer.
- **Destroy** follows ADR-020: waiters wake with `EMB_EDESTROYED` only with `EMB_OBJ_ABORT_WAITERS`; items still in the ring are simply dropped (with `OWNERSHIP`, their blocks are returned to the pool).
- ISR callers use `EMB_NO_WAIT` (SPEC-001 §5.2); a full queue returns `EMB_ETIMEDOUT`, never drops silently (RIOT's `msg_try_send` drop is the behavior to avoid, R-001 §6).

### 2.3 Why copy by value

A copy of up to 64 bytes under the lock is cheaper and simpler than a reservation protocol, and it keeps the kernel free of application pointers that may dangle. Zero copy is the explicit, ownership-tracked path of §4, never an implicit optimization.

## 3. Pipe (`CONFIG_EMB_PIPE`)

### 3.1 Interface

```c
typedef struct emb_pipe_attr {
    const char *name;
    uint8_t    *buffer;         /* caller-provided ring, `size` bytes */
    size_t      size;
    size_t      notify_trigger; /* binding fires when fill rises to >= trigger (default 1) */
    uint8_t     flags;          /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO | EMB_PIPE_SPSC */
} emb_pipe_attr_t;

emb_status_t emb_pipe_init(emb_pipe_storage_t *storage, const emb_pipe_attr_t *attr, emb_pipe_t *out);
emb_status_t emb_pipe_destroy(emb_pipe_t p);
emb_status_t emb_pipe_write(emb_pipe_t p, const void *data, size_t len, emb_timeout_t timeout, size_t *out_written);   /* @ctx thread; thread isr with EMB_NO_WAIT */
emb_status_t emb_pipe_read(emb_pipe_t p, void *buf, size_t len, size_t min_len, emb_timeout_t timeout, size_t *out_read);
emb_status_t emb_pipe_close(emb_pipe_t p);                      /* @ctx thread isr: no more writes; readers drain then see EMB_ESTATE */
emb_status_t emb_pipe_reset(emb_pipe_t p);                      /* @ctx thread: discard contents, wake waiters with EMB_ESTATE, reopen */
size_t       emb_pipe_fill(emb_pipe_t p);
emb_status_t emb_pipe_bind_notify(emb_pipe_t p, emb_thread_t t, uint8_t bit);
```

### 3.2 Semantics

- **Write** copies as much as fits, then, if `len` bytes were not all written and the timeout allows, blocks on the writers' queue until space appears, repeating until all bytes are written or the timeout expires; `out_written` is always set; a partial write on timeout returns `EMB_ETIMEDOUT` with the partial count. With `EMB_NO_WAIT` it writes what fits and returns `EMB_OK` (or `EMB_ETIMEDOUT` if nothing fit).
- **Read** returns as soon as at least `min_len` bytes are available (`min_len <= len`, `min_len >= 1`), reading up to `len`; otherwise it blocks until `min_len` is available or the timeout expires (`out_read` holds what was read, possibly nonzero on timeout when `min_len` was not reached but some bytes were). After `close`, a read returns the remaining bytes with `EMB_OK` and, once drained, `EMB_ESTATE`.
- **Chunking.** Copies run inside the object's critical section in chunks of at most `CONFIG_EMB_PIPE_CHUNK` bytes (default 32); between chunks the section is released. A write of at most one chunk is atomic with respect to other writers; longer writes may interleave with other writers at chunk boundaries. A pipe is a byte stream, not a framing mechanism: framing uses a message queue, or the `EMB_PIPE_SPSC` flag.
- **`EMB_PIPE_SPSC`.** Exactly one writer and one reader are promised by the application (checked builds record and verify the identities). The data copy then runs **without** the critical section, publishing the head or tail index after the copy with release semantics (FreeRTOS stream buffer design, R-001 §6); only the wake-up registration takes the critical section. This is the fast path for UART-class producers and consumers (03 §6.4).
- **Wake-ups.** A write that raises the fill past a blocked reader's `min_len` wakes it with hand-off of the available count (the reader then copies in its own context, so no data copy is attributed to the writer); a read that frees space wakes the first blocked writer. Readers are served in wait order; a blocked reader keeps its position until satisfied or timed out.
- **Binding** fires when the fill rises from below `notify_trigger` to at least it, unless a blocked reader consumed the transition.
- **Close** stops writers (`EMB_ESTATE`), wakes blocked writers with `EMB_ESTATE`, and lets readers drain. **Reset** discards the contents, wakes every waiter with `EMB_ESTATE`, and reopens the pipe.

## 4. Buffer pool

### 4.1 Interface

```c
typedef struct emb_pool_attr {
    const char *name;
    void       *storage;        /* block_count * block_size bytes, aligned to EMB_POOL_ALIGN */
    uint16_t    block_size;     /* >= sizeof(void *) and a multiple of EMB_POOL_ALIGN */
    uint16_t    block_count;
    uint8_t     flags;          /* EMB_OBJ_ABORT_WAITERS | EMB_OBJ_FIFO */
} emb_pool_attr_t;

#define EMB_POOL_STORAGE(name, block_size, block_count)

emb_status_t emb_pool_init(emb_pool_storage_t *storage, const emb_pool_attr_t *attr, emb_pool_t *out);
emb_status_t emb_pool_destroy(emb_pool_t p);
emb_status_t emb_pool_alloc(emb_pool_t p, emb_timeout_t timeout, void **out_block);     /* @ctx thread; thread isr with EMB_NO_WAIT  @time O(1) */
emb_status_t emb_pool_alloc_until(emb_pool_t p, emb_instant_t deadline, void **out_block);
emb_status_t emb_pool_free(emb_pool_t p, void *block);                                   /* @ctx thread isr  @time O(1) */
emb_status_t emb_pool_transfer(emb_pool_t p, void *block, emb_thread_t to);              /* @ctx thread isr  checked builds: move ownership */
uint16_t     emb_pool_available(emb_pool_t p);
emb_status_t emb_pool_bind_notify(emb_pool_t p, emb_thread_t t, uint8_t bit);            /* exhausted -> available */
```

### 4.2 Semantics

- The free list is intrusive through the first word of each free block: `alloc` and `free` are O(1) (KRN-IPC-007). `block_size` is at least a pointer and a multiple of the alignment; `EMB_POOL_STORAGE()` declares correctly sized storage.
- `alloc` on an exhausted pool blocks on the pool's wait queue; `free` with a waiter hands the block to the first waiter (hand-off, no free-list traffic, no binding bit); `free` without a waiter pushes the block and, if the pool was exhausted, signals the binding.
- `free` of a pointer outside the pool's storage, misaligned, or already free (checked builds keep a free bitmap) is misuse (`EMB_FAULT_API_ARGUMENT`, `EMB_EINVAL`).
- Destroy with blocks outstanding is misuse (`EMB_EBUSY`): the kernel cannot reclaim memory it does not own.

### 4.3 Ownership (zero copy)

- In checked builds every block carries an owner: the allocating thread, or `IN_QUEUE` while it travels in an `EMB_MSGQ_OWNERSHIP` queue, or the thread named by `emb_pool_transfer`. `free` and `transfer` by a non-owner, and `send` of a block the sender does not own, fault (`EMB_FAULT_API_OWNER`); release builds track nothing and pay nothing (KRN-IPC-006).
- The rule for application code is the one 03 §6.5 states: after sending a block, the sender no longer touches it; the receiver frees it or sends it on. `emb_pool_transfer` covers hand-over paths that do not go through a queue (a callback that will free the block).
- A thread that terminates while owning blocks (checked builds) is a lifecycle fault in the same way as a mutex owner (KRN-SYNC-025 pattern); release builds leak the block, which the statistics of §8 expose as "blocks outstanding".

## 5. Ports (FUTURE)

A port is the cross-partition and cross-core message endpoint of 03 §6.6. It is not specified for 1.0; this section fixes only what earlier specifications already decided so that the queue API does not drift away from it:

- A port presents `send`, `receive`, timeout, wake, and binding semantics identical to a message queue (KRN-IPC-008); within one image and partition a port is a message queue.
- A request may carry up to `CONFIG_EMB_PORT_MAX_LEASES` lease descriptors over the client's memory; the server accesses leased memory through validating kernel calls while the client is blocked in the request; the client's wake revokes every lease (ADR-032, KRN-IPC-009). Messages up to the copy threshold (default 64 bytes, the same `CONFIG_EMB_MSGQ_MAX_ITEM`) are copied.
- A server may run on the client's budget across a port (ADR-029 donation, FUTURE).
- Cross-core transport: a shared-memory ring plus a doorbell interrupt (04 §8.2).

## 6. Choosing among them

| Need | Use |
|---|---|
| Completion flag from an ISR | notification (SPEC-006) |
| Small fixed records, several producers or consumers | message queue |
| Records larger than 64 bytes, or a DMA destination | buffer pool block, pointer through an ownership queue |
| Byte stream from a UART-class source | pipe, `EMB_PIPE_SPSC` when one producer and one consumer |
| Several sources into one consumer | bindings on each source, one notification wait |
| Across partitions or cores | port (FUTURE) |

## 7. Misuse and checked-build diagnostics

| Condition | Checked build | Release build |
|---|---|---|
| `item_size` zero or above `CONFIG_EMB_MSGQ_MAX_ITEM`; misaligned or undersized ring; `min_len > len` or zero | fault `EMB_FAULT_API_ARGUMENT` | `EMB_EINVAL` |
| blocking form from an ISR without `EMB_NO_WAIT` | fault `EMB_FAULT_API_CONTEXT` | `EMB_EPERM` |
| `free`, `transfer`, or ownership `send` by a non-owner; double free; pointer outside the pool | fault `EMB_FAULT_API_OWNER` or `_ARGUMENT` | `EMB_EPERM` / `EMB_EINVAL` (release builds do not detect ownership) |
| second writer or reader on an `EMB_PIPE_SPSC` pipe | fault `EMB_FAULT_API_CONTEXT` | undefined (documented) |
| write to a closed pipe; read of a closed, drained pipe | runtime condition | `EMB_ESTATE` |
| destroy with waiters without `ABORT_WAITERS`; pool destroy with blocks outstanding | fault `EMB_FAULT_API_LIFECYCLE` | `EMB_EBUSY` |
| ring or free-list corruption detected | fault `EMB_FAULT_KERNEL_INVARIANT` | undefined |

## 8. Observability

Trace events: `msgq_send(q, handed_to | slot)`, `msgq_receive(q, from_slot | handed_from)`, `msgq_full_block(q, thread)`, `pipe_write(p, n)`, `pipe_read(p, n)`, `pipe_close(p)`, `pool_alloc(p, block, thread)`, `pool_free(p, block)`, `pool_transfer(p, block, to)`. Statistics (optional): per queue and pipe, high-water occupancy, blocked-send count; per pool, minimum availability and blocks outstanding; the debug descriptor exports the ring and free-list layout so a debugger can list a queue's contents and a pool's owners.

## 9. Tiny profile

Message queues and pools are available (`uint8_t` counts when `CONFIG_EMB_IPC_SMALL=y`); pipes are off by default; `CONFIG_EMB_MSGQ_MAX_ITEM` defaults to 16; ownership tracking exists only in checked builds and costs nothing otherwise.

## 10. Reference model

Message queues repeat the semaphore pattern on two wait queues with hand-off in both directions; they are added to `tools/model` when the conformance tests for them are written (M2), with the property "every sent item is received exactly once or remains in the ring" mirroring the semaphore's unit conservation. Pools likewise ("every block is owned by exactly one party or free"). Pipes are not modeled: their correctness properties are byte conservation and ordering, which the conformance suite checks directly.

## 11. Decisions taken at acceptance (2026-10-07)

1. Message queues copy by value with the copy inside the critical section, bounded by `CONFIG_EMB_MSGQ_MAX_ITEM` (default 64, tiny 16). Larger payloads travel as pool blocks by pointer.
2. Direct copy into a waiting receiver and slot hand-off to a waiting sender; a woken thread never re-checks.
3. `send_front` is `EMB_NO_WAIT` only.
4. Pipes copy in chunks of `CONFIG_EMB_PIPE_CHUNK` (default 32) under the critical section; writes longer than a chunk may interleave with other writers; `EMB_PIPE_SPSC` gives the lock-free copy path with one promised writer and reader.
5. Pipe reads take a per-call `min_len`; the binding trigger is a per-pipe attribute.
6. A closed pipe drains then returns `EMB_ESTATE`; reset wakes waiters with `EMB_ESTATE` and reopens.
7. Buffer pools are intrusive O(1) free lists; ownership is tracked in checked builds only, including the `IN_QUEUE` state and `emb_pool_transfer`.
8. No mailbox type: a message queue of pointers, or an ownership queue, is the mailbox.
9. Ports stay FUTURE; §5 fixes only the compatibility constraints already decided.
10. Message queues and pools join the reference model with their conformance tests in M2; pipes are not modeled.
