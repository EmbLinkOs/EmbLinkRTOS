# Diagnostic identity local validation

Baseline: 23cdea610fdffd088e33a4667e6bec124d53228e (IRQ PR #15).
The sibling implementation commit contains these logs. GCC 14.2.0, Linux x86-64.

For each native-gcc/native-release/native-tiny/native-periodic preset:
`cmake --preset PRESET && cmake --build --preset PRESET && ctest --preset PRESET --output-on-failure`.
Model: `.venv/bin/python -m pytest -q tools/model` (69 passed).

The before logs run the same identity fixture copied into an unmodified baseline
archive with only its CMake test registration added. Tiny fails reprioritization;
base fails uniqueness and bounded admission. The base test stops after detecting
failed exhaustion, so its later alias failure also reflects the still-live exhausted
baseline objects; it is not an independent clean-lifetime finding.

The final additional rejected-destroy assertion was checked separately in native
checked/release builds. Mixed EmbCC-kernel/GCC-host checked also passed 14 targets.
The implementation report records layout, cross-port and evidence limits.

The initial local logs belong to the first implementation commit 0d2f0a9. Later
small-table flag packing and the real-time fixture correction are covered by
CI source ea8ee9947663bb884c7fb695762c0ff785ad1035; `ci/manifest.json`
records that run and raw footprint JSON. The original thresholds are unchanged.
