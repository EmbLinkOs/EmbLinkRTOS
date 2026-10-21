# Numeric API fault attribution: local validation

Baseline: ea8ee9947663bb884c7fb695762c0ff785ad1035 (stable identity PR #16).
The implementation commit carrying these logs provides the source under test.
Linux x86-64, GCC 14.2.0; mixed build: EmbCC kernel at pinned a3c3ef4c81f9de977b8741ed16072cadca921788, GCC POSIX host/link.

For native-gcc/native-release/native-tiny/native-periodic/native-embcc:
`cmake --preset PRESET && cmake --build --preset PRESET && ctest --preset PRESET --output-on-failure`.
The mixed preset specifies `-DEMB_KERNEL_COMPILER=/workspace/EmbCC/embcc` at configure.
All five pass 15 targets. Model: `.venv/bin/python -m pytest -q tools/model` (69 passed).
Registry: `python3 -I -m unittest discover -s tools/fault -p 'test_*.py' -v` (8 passed).
Strict docs: `.venv-docs/bin/python tools/docs/build.py` (361 requirements, 82 APIs).

Location strings disabled: configure a separate native build with
`-DEMB_SET=EMB_FAULT_WHERE=n`, build test_fault and run `ctest -R '^test_fault$' --verbose`.
Its eight cases pass, including complete records and owner-death helper attribution.

The before archive uses the unchanged baseline kernel, with the new public constant
header and initial seven-case fault fixture copied in and registered in CMake.
Four API-record cases fail with the hook's mismatch exit 97; generic-zero,
successful-check and status-policy cases pass. The final owner-death assertion was
added afterward; baseline execution does not claim that extra case.
The complete suite logs predate that last additional assertion; the focused final
checked and no-where logs include it. Embedded negative checks need fork and are
not physical fault-entry evidence.

Follow-up validation at merge source 06149bf87706654bc9138445961efed3d94e3956:
a 16-level tiny table and the mixed EmbCC tiny preset each pass 15 targets;
registry/decoder checks pass eight. The `ci/` directory captures all ten footprint
artifacts from the initial API source eae71c8dbb7143b201826c8247686015dd06a840,
with successful cross-port jobs and the then-current native/aggregate status.
Final required checks remain independently attached to the PR head.
