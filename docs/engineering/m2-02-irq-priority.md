# M2-02: Portable kernel-aware IRQ priorities

**Implementation source:** `6e12f88`. **Evidence date:** 2026-10-21. This implements the IRQ-domain portion of the [contract review](m2-02-contract-review.md), following the joint walkthrough and [ADR-042](../architecture/06-decision-records.md#adr-042-explicit-kernel-aware-irq-domains-and-cortex-m-translation). Thread indexes, public fault API IDs, POINTER lifetime and other M2-02 items remain separate work.

## Behavior and compatibility

`emb_irq_set_level()` now follows larger-is-more-urgent within each port's advertised domain. `EMB_IRQ_LEVEL_MAX` and `EMB_IRQ_LEVEL_KERNEL_MAX` identify that domain; they are equal because current ports expose kernel-aware levels only. Fixed-level native, AVR and RV32 accept level 0 and reject nonzero values. This does not make their physical sources equal in urgency or alter native timer-first delivery, AVR vector ordering or current RV32 dispatch.

The public entry checks context, interrupt number and level in that order. ISR calls fault with `EMB_FAULT_API_CONTEXT` when checked and return `EMB_EPERM` in release. Invalid numbers/levels fault with `EMB_FAULT_API_ARGUMENT` (argument 1/2) when checked and return `EMB_EINVAL` in release. Port calls return errors even when checked, allowing direct verification that invalid requests leave the priority register unchanged. No invalid level is clamped or silently accepted.

This deliberately changes previously reversed Cortex-M mappings and ignored fixed-port requests. Applications must use advertised limits and review numeric levels. The limits are derived configuration outputs; conflicting explicit overrides fail. A successful level-0 call on a fixed-level port does not imply programmable preemption.

## Shared Cortex-M mapping and board data

The board declares `CONFIG_EMB_CM_IRQ_PRIORITY_BITS`: the high priority bits the port exposes, either the hardware width or a conservative subset. MPS2 references expose three; STM32F446 exposes four, as specified by [ST's device header](https://github.com/STMicroelectronics/cmsis-device-f4/blob/master/Include/stm32f446xx.h). QEMU may implement more bits than the reference declaration; that does not extend the physical-board claim.

Let `S = 1 << (8 - bits)` and `B` be the nonzero aligned `CONFIG_EMB_CM_KERNEL_BASEPRI`. Reserve the least-urgent implemented encoding for PendSV, then calculate:

```text
least urgent kernel-aware encoding = 256 - 2*S
maximum generic level = (256 - 2*S - B) / S
encoding(level) = 256 - 2*S - S*level
```

| Board configuration | Bits exposed | BASEPRI | Generic range | Level 0 / maximum encodings |
|---|---|---|---|---|
| MPS2 AN385 / AN386 | 3 | `0x20` | 0–5 | `0xC0` / `0x20` |
| NUCLEO-F446RE | 4 | `0x20` | 0–12 | `0xE0` / `0x20` |

`PRIGROUP=0` ensures the exposed high bits are preemption bits. The port accepts 3–7 exposed bits; a physical eighth bit is subpriority with this grouping and is not advertised. Configuration rejects zero/misaligned/out-of-domain thresholds, invalid default IRQ encodings and conflicting public limits. The legacy raw `CONFIG_EMB_CM_IRQ_PRIORITY` remains the external-IRQ default. `CONFIG_EMB_CM_TIMER_LEVEL` is generic, defaults to the derived maximum, and can select another kernel-aware level. A single-level Cortex-M configuration is permitted when the threshold equals the least-urgent available kernel-aware encoding.

Raw kernel-independent vector setup is not implemented. Enabling `CONFIG_EMB_IRQ_ZERO_LATENCY` is refused on this Cortex-M port; this avoids running an unmasked raw handler through kernel-aware entry hooks. Generic hardware-description generation also remains planned. Board priority-width declarations are trusted configuration inputs and must match the hardware or a conservative subset; schema input alone is not physical verification.

## Regression and evidence

`test_irq_priority` is a new conformance executable. The initial four-case native release fixture failed on baseline `3eb8783`: both the public API and port returned success for invalid levels. It passes after correction. The native checked cases additionally validate fault class/argument and context-before-argument precedence. They intentionally do not claim API attribution: `api_id` remains zero.

Cortex-M cases read back all advertised encodings, check the two endpoints, verify invalid calls leave the register unchanged, and inspect priority grouping, PendSV and SysTick settings. Behavioral cases pend two controller lines while masked and observe the higher generic level first; pend a higher-level IRQ inside a lower-level handler and observe nested delivery; verify equal levels do not preempt; and verify the highest kernel-aware level remains masked by the kernel lock. Multiple-level cases compile only when the configured domain contains multiple levels. Tests use dedicated idle controller lines 30/31 on MPS2 and reserved F446 lines 61/62. Fork-based checked-fault cases remain native-only; their embedded skips must not be described as executed misuse verification.

Local checked/release/tiny/periodic native builds each passed all 13 CTest targets. The actual pinned EmbCC-kernel/GCC-host checked preset also passed 13/13; its scope remains the [experimental mixed build](embcc-native-linux.md). The nine configuration/encoding checks and 68 model/tooling tests passed. Native interrupt delivery/model semantics are unchanged; Cortex-M nested interrupt ordering is outside that native model's proof scope. Storage and launcher contract checks, formatting, banned-construct and workflow checks passed.

[Initial CI run 39652273591](https://github.com/EmbLinkOs/EmbLinkRTOS/actions/runs/39652273591), source `6e12f88`, passed all required jobs. The controller jobs passed 10 CTest images on MPS2 M3 and 11 each on MPS2 M4 checked/release/tiny and F446 EmbSIM, including `test_irq_priority` in every row. Logs and job identities are archived. Subsequent CI emits individual successful controller cases as well as target outcomes. No physical NUCLEO run, physical preemption timing, timer-jitter qualification, complete kernel-independent class or generic hardware generator is claimed. The current MPS2/F446 tests do not qualify every Cortex-M part or board override.

The [evidence archive](https://github.com/EmbLinkOs/EmbLinkRTOS/tree/main/tools/irq/evidence/priority-2026-10-21) preserves the baseline fixture/failure, current local commands, per-case output, resolved configurations, source/runner/toolchain identities and original logs. Readable logs strip trailing whitespace; `raw-logs.json` preserves original text and the manifest records hashes.

To repeat the focused local checks:

```sh
python3 -I -m unittest discover -s tools/kconfig -p 'test_*.py' -v
cmake --preset native-gcc
cmake --build --preset native-gcc
ctest --preset native-gcc -R '^test_irq_priority$' -V
```

Use the corresponding Cortex-M configure/build/test preset and installed cross compiler/emulator for controller tests. Passing an encoding calculation on the host is separate from observed controller delivery.
