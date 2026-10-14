# EmbCC capability review, 2026-10-14

Upstream source: `EmbLinkOs/EmbCC` at `a3c3ef4c81f9de977b8741ed16072cadca921788`. RTOS source: `2e59c2a37b36c90e5f15892fb258c8a2e21be306`. See [the review](../../../../docs/engineering/embcc-capability-review.md) for the ten legacy items, evidence scope and remaining integration work.

`manifest.json` retains commands, exit codes, stdout/stderr, ten check outcomes, tool versions, fixture/probe and binary hashes. Sources beside it are the local probe inputs; `executed-probes.py` is the exact environment-specific driver, with absolute paths and a fresh output directory requirement. It is an investigation artifact, not a reusable CI test or an installed tool. Upstream fixtures are identified by their paths/hashes and the pinned commit. Generated objects/images and compiled tool binaries are not committed.

Readable `.log` files strip trailing whitespace for repository checks. `raw-build-logs.json` retains their complete original text; the manifest records both original per-log hashes and normalized-file hashes. Probe-command stdout/stderr remains unchanged in the manifest.

Three firmware checks used EmbSim: upstream's AVR linker-script fixture, its AVR `__flash` fixture, and a local small-atomic probe on RV32 and AVR. Their sentinels and expected values were checked, with finite instruction and wall-time limits. This is simulator evidence, not physical timing, QEMU replay, CMSE security qualification, interrupt-race coverage or an RTOS conformance run. The AVR debugger check executes the real register-layout function on the host; it does not establish an attached remote session.

`embcc-build.log` records the successful local host bootstrap with a POSIX feature macro and warnings enabled but not treated as errors. Default/strict GCC 14.2 bootstrap attempts failed; the manifest names their observed causes. No upstream source was patched.

`embcc-rtos-configure.log` records the executable-link configure failure when the Linux target's `crt1.o` was not built/installed. `embcc-rtos-static-configure.log` records the subsequent successful compile-only CMake configure; `embcc-rtos-static-build.log` records its storage-probe failure on missing `EMB_LAYOUT_MARKER`. The static override isolates the portability-header gap; it does not bypass that failure or qualify a Linux RTOS executable. No complete EmbCC RTOS image was tested.
