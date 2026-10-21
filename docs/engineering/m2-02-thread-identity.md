# M2-02: Stable diagnostic thread identity

The implementation separates identity from base/effective priority (KRN-THR-025, SPEC-008 §3, ADR-043). The scheduler still uses priority slots on tiny; traces and fault records use the independent TCB index. Indexes remain reserved from successful init through successful destroy, including suspend, inheritance, exit, join and detach. They can be reused after destroy. Caller-owned storage must remain alive, with external synchronization against destroy/reuse; this does not solve POINTER-handle ABA or active-storage double initialization.

## Allocation and cost

The list scheduler has 254 non-idle IDs (0–253), a 32-byte static bitmap replacing a one-byte counter, and a scan bounded by 254 bits under the scheduler lock. Tiny has `CONFIG_EMB_PRIORITY_COUNT - 1` IDs and a bitmap of `ceil((priority_count - 1)/8)` bytes; the default eight priority levels need one byte. For table configurations with at most eight priority levels, independent IDs 0–6 occupy the three unused high object-flag bits and 7 encodes idle. Existing notification/result updates preserve those bits. Larger table and list configurations have a separate byte, with actual total size determined by alignment. Idle always uses 0xFE, while invalid handles and non-thread fault context use 0xFF. Query and release are O(1).

All argument checks and tiny's occupied-priority check precede ID reservation. Exhaustion returns `EMB_ENOMEM` in checked and release builds before TCB/stack construction and leaves the output null. Tiny's full table instead reports its existing `EMB_EBUSY`. Successful destroy releases the ID in the same critical section that invalidates object state. Rejected destroys keep their reservation.

## Verification

`test_thread_identity` checks priority changes, unique live/terminated IDs, invalid handles and 300 construct/destroy cycles on each port. Native list-scheduler builds also fill all 254 slots, verify uniqueness, check `EMB_ENOMEM`/null output/untouched storage, and prove admission resumes with the sole released ID. Existing thread and mutex tests check stability across suspend/resume and inheritance/restoration.

The differential runner declares `idle_index=254`. The bridge retains legacy tiny idle-slot decoding only when that field is absent; its regression checks both interpretations, including a real new thread with ID 0. Historical archives are unchanged. Model scheduling semantics are unchanged.

Local GCC checked/release/tiny/periodic builds each pass 14 CTest targets; the model/tooling suite passes 69 tests, including historical archive replay. The same identity fixture fails against baseline `23cdea610fdffd088e33a4667e6bec124d53228e`: tiny changes ID 2 to 5 on reprioritization; base duplicates a live ID and admits a 255th non-idle object rather than returning `EMB_ENOMEM`. These are executed regressions, not just source analysis.

Native GCC tiny TCB storage remains 256 bytes, as in the baseline. Initial AVR CI measured 33 bytes with a separate byte, exceeding the 32-byte T1 gate. Packing only the small table profile's independent ID into unused flag bits preserves that resource target without coupling identity to priority. Cross-port results and other measured ABI layouts are recorded after CI; physical hardware execution remains unclaimed.
