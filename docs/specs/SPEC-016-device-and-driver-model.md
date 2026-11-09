# SPEC-016 - Device and Driver Model: Instances, Lifecycle, Requests and Binding

**Implementation and release scope:** [Project status](../STATUS.md) records the implemented subset; [roadmap](../architecture/07-roadmap.md) defines release gates. Nothing in this specification is implemented yet; M4.1 implements it.

**Status:** Proposed 2026-11-09 for the project owner's review (M4.0).
**Requirements:** `docs/requirements/DRV.md` (DRV-001 to 010); PWR-002.
**Builds on:** 04 §2 and §3; ADR-004 (hardware as data), ADR-007 (request and completion), ADR-051 (any board), ADR-057 (the RP2350); SPEC-001 (API conventions, status codes), SPEC-006 (notifications, work), SPEC-014 §4 and §6 (board `devices:`, `soc_devices.c`), SPEC-015 §5 (debug descriptor).
**Classes:** SPEC-017 specifies each driver class on this model.

---

## 1. Scope and terms

| Term | Meaning |
|---|---|
| **Device** | One instance of a peripheral or an external part, as the board description names it: USART2, SPI2, a BME280 on I2C1 |
| **Driver** | The code for one `compatible`: `st,stm32-usart`, `arm,pl022` |
| **Class** | The API a kind of device offers, whatever its driver: UART, SPI, I2C, DMA, watchdog, flash, RTC, entropy (SPEC-017) |
| **Request** | One operation submitted to a device, with its buffers, parameters and completion target |
| **Completion target** | What the driver signals when a request ends: a notification, a semaphore, a work item, or a bounded callback |
| **Binding table** | `drivers/bindings.yaml`: for each `compatible`, its class, its sources and how its configuration comes from the description |

Out of scope: device placement in isolated partitions (the `partition` field of 04 §2.1) waits for M3b; hot-plug and runtime device creation are not supported, by design.

## 2. The device instance

A device is a `const` object the generator emits; nothing creates one at run time.

```c
typedef struct emb_device {
    const char *name;                 /* the description's name, for diagnostics only */
    uint16_t class_id;                /* EMB_DRV_CLASS_* (SPEC-017) */
    uint8_t level;                    /* EMB_DEVICE_EARLY or EMB_DEVICE_KERNEL (§4) */
    uint8_t flags;                    /* EMB_DEVICE_POLLED_ONLY, ... */
    const void *ops;                  /* the class's operations table, filled by the driver */
    const void *config;               /* generated, const: bases, interrupts, pins, clocks, DMA */
    struct emb_device_state *state;   /* RAM */
    void *data;                       /* the driver's private RAM, sized by the binding */
} emb_device_t;

typedef struct emb_device_state {
    uint8_t lifecycle;                /* EMB_DEVICE_UNINIT ... (§3) */
    uint8_t power;                    /* the device's power state (SPEC-018) */
    int16_t last_status;              /* the status that moved it to FAILED, or EMB_OK */
} emb_device_state_t;
```

- **Reaching a device** is by symbol, never by string: `EMB_DEVICE(spi2)` names the generated `emb_dev_spi2`. A missing device is a link error, not a run-time failure.
- **`config` and `ops` are `const`** and stay in flash. `state` and `data` are the only RAM a device costs, with the request queue of §5.
- **The application never writes a device's fields.** It calls the class API with the device.

## 3. Lifecycle

```
UNINIT -> INITIALIZING -> READY <-> SUSPENDED
                 |          |
                 v          v
              FAILED     FAILED
```

| State | Entered | An operation returns |
|---|---|---|
| UNINIT | at reset | `EMB_ESTATE` |
| INITIALIZING | while its `init` runs | `EMB_ESTATE` |
| READY | `init` returned `EMB_OK` | the operation's own result |
| SUSPENDED | `emb_device_suspend()` or the power policy | `EMB_ESTATE`, unless the class resumes on demand (SPEC-018) |
| FAILED | `init`, `resume` or the driver reported an unrecoverable error | `EMB_EIO`; `last_status` holds the cause (DRV-003) |

- **A FAILED device does not stop the others.** Initialization continues with the next device. A device that depends on a FAILED one is not initialized: it becomes FAILED with `last_status` `EMB_ENOENT`.
- **Observable** through `emb_device_state(dev)` and `emb_device_ready(dev)`, and through the debug descriptor (§9) (DRV-002).
- **Transitions happen under the device's lock** (§6), so a request never sees a state change half-done.

## 4. Initialization levels and order

| Level | When | What may run |
|---|---|---|
| **EARLY** | from `emb_board_init()`, before `emb_kernel_init()` | polled code only: no kernel object, no interrupt (DRV-007). The clock tree, GPIO and pin control, the console, and the flash for a bootloader |
| **KERNEL** | from `emb_kernel_start()`, after the kernel objects of the init table exist and before the first thread runs | everything: interrupts, semaphores, DMA |

- **The order within a level is a topological sort** the generator computes from each device's dependencies (DRV-001): its `deps`, its `clock` and `reset` references, its bus (`bus: spi1` for a part on a bus), its DMA controller and its interrupt controller. Ties keep the description's order, so the order is deterministic.
- **A cycle is a generation error**, naming the devices on it. So is an EARLY device that depends on a KERNEL one.
- **The generator emits one array per level**, `emb_soc_devices_early[]` and `emb_soc_devices_kernel[]`, in order, and the board support walks them. The kernel's linker init table (`emb_init_table`) is not used for devices, because a linker table has no order.

## 5. Requests and completion (ADR-007)

Every class operation is a request submitted to a device, completed asynchronously (DRV-004).

```c
typedef struct emb_drv_request {
    struct emb_drv_request *next;     /* the device's queue: the driver's, not the caller's */
    emb_status_t status;              /* EMB_EBUSY while queued or in progress, then its result */
    uint32_t done;                    /* bytes or items completed, also on error and cancel */
    emb_drv_completion_t completion;  /* §5.1 */
    /* the class's own fields follow, in the class's request type (SPEC-017) */
} emb_drv_request_t;
```

- **The caller owns the request's memory** until it completes. The driver links it into the device's queue. Nothing is allocated (KRN-MEM).
- **One queue per device,** first in first out. A class may say how many requests run at once; most run one.
- **Submit** checks the device's state and the request's parameters, queues it and returns `EMB_OK`, or an error at once without queuing.
- **Complete** (`emb_drv_complete(dev, req, status)`, called by the driver, from an interrupt or a thread) sets `status` and `done`, delivers the completion target and starts the next queued request.

### 5.1 Completion targets (DRV-005)

| Target | What completion does | Allowed from |
|---|---|---|
| `EMB_DRV_NOTIFY(thread, bits)` | `emb_notify_set(thread, bits)` | every caller |
| `EMB_DRV_SEM(sem)` | `emb_sem_give(sem)` | every caller |
| `EMB_DRV_WORK(queue, item)` | `emb_work_submit(queue, item)` | every caller |
| `EMB_DRV_CALLBACK(fn, arg)` | calls `fn(req, arg)` in the completing context, possibly an interrupt | only code that accepts interrupt context; the callback must be bounded and must not block |
| `EMB_DRV_POLL` | nothing; the caller polls `status` | polled mode and the bootloader |

### 5.2 Blocking calls are wrappers

A blocking class call (`emb_drv_uart_write(dev, buf, len, timeout)`) is a wrapper over submit (DRV-004):
1. it builds the request on the caller's stack, with a binary semaphore in the caller's stack frame as its target;
2. submits it and takes the semaphore with the caller's timeout;
3. on timeout, cancels the request (§5.3) and waits for its completion, which is then certain and bounded, and returns `EMB_ETIMEDOUT` with `done` telling how much was transferred.

The wrapper's cost is a request and a semaphore's storage on the caller's stack. A blocking call from an interrupt or before the kernel is refused with `EMB_EPERM` (SPEC-001 §5.3); the polled API (§7) is the alternative there.

### 5.3 Cancellation and timeout (DRV-006)

- `emb_drv_cancel(dev, req)`: a queued request is removed and completes with `EMB_ECANCELED`. A request in progress is stopped as its class specifies, then completes with `EMB_ECANCELED` and `done` set to what was transferred.
- **Each class specifies** (SPEC-017) what stopping in progress means for its hardware: the state of the peripheral, whether a DMA transfer's partial data is in memory, and that the next request starts from a clean state.
- **A request completes exactly once,** whichever comes first, the transfer's end or its cancellation; the other is a no-op.

## 6. Concurrency

- **Each device has one lock domain**, a critical section the driver holds while it changes the queue or the state. Its interrupt handler and its submit share it. Holding it is bounded: no transfer runs inside it.
- **Requests from several threads** are serialized by the queue. A class that needs exclusive use over several requests (an I2C transaction with a repeated start, an SPI device's chip select held across transfers) provides a per-device acquire and release, a mutex with priority inheritance (SPEC-005).

## 7. Polled mode (DRV-007)

The EARLY classes (clock, GPIO, console UART, flash) offer polled functions, `emb_drv_uart_poll_write()` and the like, that use no kernel service, and do not need the device to be READY in the kernel's sense, only initialized at the EARLY level. They serve the boot path, the fault report and the bootloader (ADR-058). Once the kernel runs, a device's polled and request APIs must not be mixed on the same device, except by the fault report, which takes the device over.

## 8. Binding and generation (DRV-009)

`drivers/bindings.yaml` maps each `compatible` to its driver:

```yaml
st,stm32-usart:
  class: uart
  sources: [drivers/st/stm32_usart.c]
  config: emb_stm32_usart_config_t          # its fields come from the device's description
  fields: { base: base, irq: irqs.0, clock: clock, pins: pins, dma_tx: dma_requests.tx }
  data: emb_stm32_usart_data_t              # the driver's RAM, sized at generation
arm,pl022:
  class: spi
  sources: [drivers/arm/pl022.c]
  ...
```

- **The generator binds every enabled device** of the board, and fails on a `compatible` the table does not know (DRV-009).
- **It emits**, beside SPEC-014 §6's outputs:
  - `soc_devices.c`: each device's `config` initializer, its `emb_device_t` and its RAM, and the two ordered arrays of §4;
  - `soc_devices.h`: `extern` declarations, so `EMB_DEVICE(name)` resolves;
  - `EMB_BOARD_DRIVER_SOURCES` in `board.cmake`: the sources of the drivers the board's devices use, and only those.
- **The SoC's family `CMakeLists.txt` stops listing driver sources by hand.** It compiles what the board names.

## 9. The debug descriptor (DRV-002)

The descriptor grows by appended indices, as SPEC-015 §5 allows (its `version` is unchanged):
- `EMB_DEBUG_DEVICE_TABLE`: the address of an array of device pointers, in initialization order;
- `EMB_DEBUG_DEVICE_COUNT`;
- `EMB_DEBUG_DEVICE_NAME_OFFSET`, `EMB_DEBUG_DEVICE_STATE_OFFSET` and the offsets of `lifecycle` and `last_status` in the state.

`tools/descriptor` and the GDB commands list each device with its state and last status. Without `CONFIG_EMB_DEVICE`, the values are `EMB_DEBUG_ABSENT`.

## 10. Status codes (DRV-008)

- **Common to every class:** `EMB_ESTATE` (not READY), `EMB_EIO` (FAILED, or a hardware error), `EMB_ETIMEDOUT`, `EMB_ECANCELED`, `EMB_EBUSY` (exclusive use held by another), `EMB_ENOTSUP` (a capability the device does not have), `EMB_EINVAL`.
- **A class's own codes** lie in the sub-range of −64 to −127 that SPEC-017 assigns it, never reused across classes, each named by `emb_status_name()`.

## 11. Capabilities

`emb_device_caps(dev)` returns the class's capability bits for this device, such as DMA, hardware flow control or a calendar. An operation needing a capability the device lacks returns `EMB_ENOTSUP`, and an absent device class returns `EMB_ENOTSUP` too: nothing is emulated in its place (SEC-012).

## 12. Configuration and cost

- `CONFIG_EMB_DEVICE` (default `y` on the base profile, `n` on tiny and AVR, where the board's free functions stay).
- A device costs `emb_device_t` in flash, `emb_device_state_t` and its driver's data in RAM. M4.1 reports the change in the footprint of the reference images.
- **The existing STM32 free functions stay** (`emb_stm32_usart_putc()` and the others) as the drivers' low layer. The class drivers wrap them, and `examples/controller` keeps its calls.

## 13. Verification

| Requirement | How |
|---|---|
| DRV-001 | the generator's tests: dependency order, a cycle refused, an EARLY device on a KERNEL one refused, a deterministic order |
| DRV-002 | `tests/drivers/device/`: each lifecycle state through the API; the descriptor reader lists them |
| DRV-003 | a device whose `init` fails is FAILED, returns `EMB_EIO`, and does not stop the devices after it |
| DRV-004, 005 | each class: the blocking call is the wrapper; one request per completion target |
| DRV-006 | each class: cancel before, during and after a transfer, DMA included |
| DRV-007 | an image using the polled API before `emb_kernel_init()`; no kernel symbol on the polled paths |
| DRV-008 | the class headers' codes in their sub-ranges, unique |
| DRV-009 | the generator binds the reference boards' devices and refuses an unknown `compatible`; an image links only its devices' drivers |

## 14. Defaults to confirm at review

- The blocking wrapper's target is a semaphore on the caller's stack (§5.2), not a reserved notification bit: notification bits stay the application's.
- `EMB_EIO` for a FAILED device, `EMB_ESTATE` for one not yet READY or suspended.
- Devices are not in the kernel's linker init table; two generated arrays order them.
- `CONFIG_EMB_DEVICE` off on tiny and AVR.
