# M2-02: Numeric API attribution in fault records

Public API misuse faults now populate `emb_fault_info_t.api_id` with an explicitly assigned public-entry ID (SPEC-001 §5.3, ADR-044). Argument numbers, class, code, context and checked/release policy retain their existing meaning. Shared semaphore, mutex, notification and sleep helpers receive the entry ID as an ordinary parameter, so relative and absolute calls remain distinguishable. There is no mutable global current-API state and no new API-name strings in firmware.

## Registry and host use

`tools/fault/api-registry.json` assigns 82 documented public functions explicit IDs. The initial range is 0x0001–0x0052. Definitions in `include/emb/api_ids.h` and `tools/fault/api-ids.json` are generated with `python3 -I tools/fault/generate.py`; normal firmware builds consume the checked-in header without needing Python generation. Declaration order, addresses and line numbers never assign IDs. Future additions choose unused values; retired entries retain their number and name forever.

CI runs `--check` and compares the registry against the PR/push base to reject deletion, renumbering, reassignment or reactivation of retired entries. Unit tests check schema, uniqueness, range, declaration coverage, generation order independence, output drift and compatibility. Numeric IDs do not imply an API is enabled in every configuration.

`python3 -I tools/fault/decode.py 0x0016` prints `emb_mutex_lock`; 0x0017 is `emb_mutex_lock_until`. The docs publish the generated map as a download. Keep it with firmware build artifacts. EmbDebug can consume it in a later integration; no EmbDebug implementation is claimed here.

## Attribution boundary

Zero remains reserved for hardware/invariant/generic faults, explicit application `emb_fault()` and `EMB_CHECK()`. An application can deliberately raise an API-class fault without a public entry, so class alone does not imply nonzero attribution. A status checked after a call cannot identify that earlier function. Internal owner-death handling attributes its API lifecycle fault to `emb_thread_exit`, including an entry function's implicit return. Trace payload and temporary hook-record layout stay unchanged; retained crash records are not implemented by this change.

## Verification

The native checked fixture validates shared-wrapper IDs and all five API misuse classes, one-based argument numbers and context/sub-state precedence. It checks ISR context with thread 0xFF, a synthetic prekernel state inside a fork, and scheduler-locked sleep calls. It also verifies zero attribution for manual, check, invariant and hardware faults, including supplied hardware address/PC/SP. The same fixture runs with location strings disabled and checks that numeric attribution survives with `where == NULL` for API faults.

Native release and embedded builds preserve status behavior. Checked embedded fault expectations that need host fork are skipped; complete negative record checks execute on native, not physical targets. Cross builds and emulators exercise compilation and applicable common conformance; they do not qualify physical fault entry or retained storage.

Local GCC checked/release/tiny/periodic suites each pass 15 CTest targets. The experimental EmbCC-kernel/GCC-host checked build also passes 15. The focused checked record fixture passes eight cases, including owner-death helper attribution; it also passes with `EMB_FAULT_WHERE=n`. Registry/decoder tests pass eight cases, model/tooling passes 69, and strict docs build 361 requirements and 82 API declarations. The pre-attribution kernel at `ea8ee9947663bb884c7fb695762c0ff785ad1035` fails the seven-case initial fixture's four API-record cases because it reports ID zero; its generic-zero and status-policy cases pass. The final owner-death assertion was added afterward and is not claimed as executed against that baseline.

CI cross-port and footprint checks are recorded after completion. Physical hardware and retained-format qualification remain unclaimed.
