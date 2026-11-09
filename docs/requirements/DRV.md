# DRV - Device and driver model

Group `DRV`. Design: `docs/architecture/04-platform-architecture.md` §2 and §3; `docs/specs/SPEC-016-device-and-driver-model.md` and `SPEC-017-driver-classes.md` (planned, M4.0). Related groups: HW (descriptions and the generator), API (status codes), PWR (suspend and resume), OBS (the debug descriptor), KRN-NOTIF (completion targets).

DRV-001 to 006 originate in `docs/architecture/04-platform-architecture.md` §2.2 and §3.1 and are restated here as the authoritative copy. DRV-007 to 010 were added with M4's plan (2026-11-08). New requirements start at 011.

---

## Requirements

### DRV-001  Initialization order from declared dependencies
**Statement.** Device initialization order shall be computed at build time from declared dependencies.
**Rationale.** A static ordered table: no runtime dependency resolution, and a cycle is a build error (04 §2.2).
**Status.** Accepted 2026-10-07
**Verification.** Test: the generator's tests order devices from `deps` and the clock and bus references, and refuse a cycle (`hw/gen/test_hwgen.py`, planned with SPEC-016).
**Trace.** 04 §2.2; SPEC-016; HW-001

### DRV-002  Observable lifecycle
**Statement.** Device lifecycle states shall be observable through a common API and through the debug descriptor.
**Rationale.** A field failure is diagnosed from a device's state without the driver's cooperation.
**Status.** Accepted 2026-10-07
**Verification.** Test: `tests/drivers/device/` reads each state through `emb_device_state()`; the descriptor reader lists them (`tools/descriptor`).
**Trace.** 04 §2.2; SPEC-016; OBS-008; A107

### DRV-003  A FAILED device rejects operations
**Statement.** A device in FAILED state shall reject operations with a defined status rather than undefined behavior.
**Rationale.** A failed device must not take the application down with it, nor block unrelated devices (04 §2.2).
**Status.** Accepted 2026-10-07
**Verification.** Test: a device whose initialization fails returns its class's defined status to every operation, and the devices after it still initialize.
**Trace.** 04 §2.2; SPEC-016

### DRV-004  Request and completion, blocking as a wrapper
**Statement.** Every driver class shall define its operations as request submission with asynchronous completion; blocking variants shall be wrappers.
**Rationale.** One driver source serves polled, interrupt and DMA modes; cancellation and timeouts have one implementation (ADR-007).
**Status.** Accepted 2026-10-07
**Verification.** Inspection: each class in SPEC-017 has submit and complete; Test: each class's blocking call is the wrapper over submit (`tests/drivers/<class>/`).
**Trace.** 04 §3.1; ADR-007; SPEC-017

### DRV-005  Completion targets
**Statement.** Completion delivery shall support notification, semaphore, work item, and bounded ISR callback targets.
**Rationale.** The caller chooses how it is woken; notification works into an unprivileged partition (04 §3.1).
**Status.** Accepted 2026-10-07
**Verification.** Test: one request per target type completes and wakes or runs it, per class.
**Trace.** 04 §3.1; SPEC-016; KRN-NOTIF

### DRV-006  Cancellation and timeout per class
**Statement.** Request cancellation and timeout shall be defined per class, including what happens to a partially completed DMA transfer.
**Rationale.** A canceled DMA transfer leaves memory and the peripheral in a state the caller must be able to rely on.
**Status.** Accepted 2026-10-07
**Verification.** Test: per class, cancel and time out a request before, during and after its transfer, DMA included, and check the bytes transferred, the status and the peripheral's next request.
**Trace.** 04 §3.1; SPEC-017

### DRV-007  Polled mode before the kernel
**Statement.** The classes needed before the kernel starts (clock, GPIO, the console, and flash for a bootloader) shall provide a polled mode that uses no kernel service.
**Rationale.** Boot code and the bootloader (ADR-058) run before, or without, the kernel; they must use the same drivers rather than a second set.
**Status.** Proposed 2026-11-09
**Verification.** Test: an image that calls the polled operations before `emb_kernel_init()`; Analysis: the polled paths reference no `emb_` kernel symbol.
**Trace.** SPEC-016; ADR-058; BOOT-002

### DRV-008  Status codes per class
**Statement.** Each driver class shall define its own status codes inside the range API-010 gives driver classes and subsystems (-64 to -127), from a sub-range SPEC-017 assigns it, and shall never reuse another class's.
**Rationale.** A status names its cause across classes, and the values stay stable forever (API-010).
**Status.** Proposed 2026-11-09
**Verification.** Analysis: the class headers' codes lie in their sub-ranges and are unique; `emb_status_name()` names each.
**Trace.** API-010; SPEC-001 §4; SPEC-017

### DRV-009  Driver binding at generation
**Statement.** The driver for each device shall be chosen at generation time from its description's `compatible`, through one binding table; a device with no driver for its `compatible` shall be a generation error, and the image shall contain only the drivers its devices use.
**Rationale.** No lookup by string at run time, no unused driver in flash, and a missing driver found before the build (HW-003).
**Status.** Proposed 2026-11-09
**Verification.** Test: the generator binds every device of the reference boards and refuses an unknown `compatible`; Analysis: a board's image links only the drivers its devices name.
**Trace.** SPEC-016; HW-001; HW-003

### DRV-010  A class is claimed only when complete
**Statement.** A driver class shall be claimed for a board only when its specification answers every item of 04 §3.2's checklist and an archived run on that board passes its tests.
**Rationale.** A partial class presented as supported is how drivers become unreliable; the checklist and the board's evidence are the bar (ADR-051).
**Status.** Proposed 2026-11-09
**Verification.** Inspection: SPEC-017's section per class answers each checklist item; Analysis: `tools/support/matrix.py` lists a class for a board only with its archived record.
**Trace.** 04 §3.2; ADR-051; A81
