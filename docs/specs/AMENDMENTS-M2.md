# Specification amendments recorded during M2

Corrections found while preparing the M2 primitives, where an accepted specification, requirement or decision record contradicts another or the implemented behavior. Each is applied in the affected document; this page keeps the audit trail. Numbering continues from the [M1 record](AMENDMENTS-M1.md). Decisions about the new M2 primitives themselves are recorded separately, as they are taken.

| # | Document | Amendment | Where in the code |
|---|---|---|---|
| A29 | KRN-TIM-035, SPEC-003 §12 | The requirement's last clause, `EMB_EOVERFLOW` for a `_until` deadline out of the 32-bit profile's range, contradicts A2 and KRN-TIM-022: such a deadline is indistinguishable from a past instant and takes the past-deadline path. The clause is removed; the checked-build timer detections remain. | `kernel/thread/thread.c`, `kernel/sync/*.c`, `kernel/notify/notify.c` |
| A30 | SPEC-004 §9 | A blocking wait from an ISR, with the scheduler locked or inside the caller's critical section returns `EMB_EPERM` in release builds, as SPEC-001 §5.3, SPEC-005, SPEC-006 and SPEC-007 state and `EMBK_API_REQUIRE_CAN_BLOCK` implements; the table's `EMB_EINVAL` was wrong. | `kernel/include/embk/kernel.h` |
| A31 | ADR-027 | A binding costs a thread pointer and a one-bit mask, not a bit index (A23, KRN-NOTIF-012). | `kernel/include/embk/sync.h` |
| A32 | KRN-NOTIF-012 | The verification cited binding cases in `tests/conformance/test_sem.c`, which has none; the existing case is `notify_semaphore_binding_signals_ready_transition` in `tests/conformance/test_notify.c`. | `tests/conformance/test_notify.c` |
| A33 | KRN-NOTIF-009 | The requirement is accepted but only partly implemented: `EMB_EEXIST` for a second bind and dropping a set to a terminated thread exist; `unbind` and target-lifetime rules are M2-03 work. The status now says so. | `kernel/sync/sem.c`, `kernel/notify/notify.c` |
