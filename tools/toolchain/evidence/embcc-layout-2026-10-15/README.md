# EmbCC layout-marker verification

See [the correction](../../../../docs/engineering/embcc-layout-marker.md). `manifest.json` records the tested change commit, baseline, pinned compiler/binary/source hashes, all eight command groups and additional checks. No complete EmbCC image or RTOS conformance execution is claimed.

Each configuration directory retains its exact generated config/header, independent assertions, temporary toolchain and configure/storage/assertion/kernel-archive logs. Configuration/header hashes and original commands preserve the local build identity; absolute paths describe this workspace. The source fingerprints identify the files compiled, rather than assuming a documentation-only follow-up changed their behavior.

The four native headers matched GCC byte for byte. Separate logs retain the passing contract tests, deliberately failing original-header check, twelve passing native GCC CTest targets, and the actual EmbCC native build's later `pthread.h` failure. The contract test under GCC selects the EmbCC header; the actual-compiler result is recorded separately.

Readable logs strip trailing whitespace. `raw-logs.json` keeps the original text, with both raw and normalized hashes in the manifest. Kernel archives and generated binaries are not committed. These temporary toolchains and evidence are not installed/supported compiler presets or a replacement for executed port/ABI/conformance qualification.
