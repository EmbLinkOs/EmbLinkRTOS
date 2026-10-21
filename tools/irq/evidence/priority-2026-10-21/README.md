# Kernel-aware IRQ priority evidence

The local execution source is `6e12f8837b89c8572f61c56ad7ab1d1b80f51f9a`, captured with a clean tracked tree. The scoped [implementation report](../../../../../docs/engineering/m2-02-irq-priority.md) explains behavior, formulas, compatibility and remaining work.

`manifest.json` records local commands/exit codes and runner/source/configuration/toolchain identities. Five native configurations (GCC checked/release/tiny/periodic and experimental actual EmbCC kernel/GCC host checked) passed 13 CTest targets each. `priority-cases.log` records individual native cases, including expected checked-fault output. Nine real configuration/encoding tests and 68 model/tooling tests passed. Fixed-port delivery and the native model's interrupt ordering are unchanged.

`baseline-test.c` is the initial four-case fixture compiled against main `3eb8783` with native release settings. It failed two cases: out-of-range public and port calls incorrectly returned success. The archived `baseline-failure.log` records that negative control. The fixture initially includes only the four generic cases; later fault-attribution-field and Cortex-M cases belong to the implementation source, not that baseline run.

`initial-ci.json` and `initial-ci/` preserve completed QEMU Cortex-M3/M4 checked/release/tiny and F446 EmbSIM jobs from CI run `39652273591`, head `6e12f8837b89c8572f61c56ad7ab1d1b80f51f9a`. Every new `test_irq_priority` image and every existing target passed. Job logs record compiler/emulator checkout inputs and complete build/CTest outcomes; this initial run prints target outcomes rather than successful individual embedded cases. Subsequent CI logging includes those cases explicitly. Checked embedded misuse checks still require fork and are skipped; controller encoding/order/preemption/masking cases run on the emulators. A target pass must not be represented as every possible misuse path being executed.

Readable logs strip trailing whitespace. `raw-logs.json` retains original local/job output, and hashes distinguish raw command text from readable files. The manifest's file hashes cover all archived inputs and outputs except itself.

To reproduce locally, configure/build a documented preset and run CTest with `--no-tests=error`; focus on `-R '^test_irq_priority$' -V` for per-case output. Configuration tests use `python3 -I -m unittest discover -s tools/kconfig -p 'test_*.py' -v`. The existing QEMU/EmbSIM CI jobs provide the cross tools and run the same images.

These are bounded local/emulator results. They do not establish physical NUCLEO timing/jitter, all Cortex-M priority widths, an implemented raw class, generated hardware tables, stable thread IDs, public fault API IDs or generation-based POINTER protection.

`followup-ci/` records the M3 job at `7bb9c0b` adding the non-nesting configuration gate and focused single-level test. It passed the ordinary ten M3 images and the additional single-level priority image (seven registered cases, zero failures, three fork-only skipped assertions). The normal multi-level priority image has nine cases and three such skips. This is subsequent source/evidence, separate from the original clean local captures at `6e12f88`. The manifest records both source identities.
