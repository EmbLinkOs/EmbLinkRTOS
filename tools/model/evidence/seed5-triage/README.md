# Historical seed-5 triage evidence

See [`docs/engineering/m2-01-seed5-triage.md`](../../../../docs/engineering/m2-01-seed5-triage.md) for outcomes, bounds and remaining uncertainty. This is an evidence archive, not a passing-test registration or an assertion that cancellation was fixed.

- `manifest.json`: current source identity, fixed campaign budgets, four runner/build identities and 40 unfiltered campaign records. Embedded configuration contents were moved to the named adjacent `*-config.json` files without changing their bytes or recorded hashes. Original workspace paths remain descriptive.
- `*-seed-*.log`: full CLI stdout/stderr and campaign summary, with SHA-256 in the manifest. All 56,000 current executions agreed; there are no captured failing-run bundles.
- `cancellation-schedules.jsonl`: one record per preset/seed, preserving all 40 ordered placement lists, including empty/repeated schedules. The corresponding typed cancellation scenario is in each preset's manifest record. These are reconstructed from the same unchanged campaign inputs/RNG after execution, not a capture of successful-run raw traces.
- `historical/`: the four old-source campaign logs and separate identities/configuration snapshots. `archive_source` identifies the commit unpacked with `git archive`; the identity's Git SHA is null because the directory is not a Git checkout. Current tools built that source; the original historical binary/toolchain is unavailable. One seed-3 control reproduced the known old-model mismatch; all 4,200 seed-5 runs agreed.

The current-source manifest also preserves `all_inputs_sha256`: SHA-256 of `json.dumps(inputs, sort_keys=True).encode()`, where `inputs` is the ordered list of `{scenario, encoded, schedules}` for each applicable catalogue scenario. The generator consumes one seeded RNG across the complete admitted catalogue, exactly as the campaign CLI does. Filtering or changing catalogue order/applicability changes that digest.

To validate the archived current-source identities, configuration/log hashes, complete input digests and cancellation schedules without running the kernel, use this snippet from the repository root. It refuses changed model/tooling sources; a later source may need the archived commit before the reconstruction is comparable.

```sh
python3 -I - <<'PY'
import hashlib
import json
import random
import sys
from pathlib import Path
sys.path.insert(0, str(Path('tools/model').resolve()))
import bridge
import evidence

archive = Path('tools/model/evidence/seed5-triage')
manifest = json.loads((archive / 'manifest.json').read_text())
assert manifest['complete']
saved = {(r['preset'], r['seed']): r['schedules']
         for r in map(json.loads, (archive / 'cancellation-schedules.jsonl').read_text().splitlines())}
for preset in manifest['presets']:
    identity = preset['identity']
    assert preset['identity_consistent']
    for path, expected in identity['source_sha256'].items():
        assert evidence.sha256(path) == expected, path
    config = archive / identity['build']['configuration_archive']
    assert evidence.sha256(config) == identity['build']['config_sha256']
    assert evidence.configuration_values_sha256(json.loads(config.read_text())) == identity['build']['configuration_values_sha256']
    for campaign in preset['campaigns']:
        assert evidence.sha256(archive / campaign['log']) == campaign['log_sha256']
        rng = random.Random(campaign['seed'])
        inputs = []
        for factory in bridge.catalogue.ALL:
            original = factory()
            if not bridge.applicable(original, preset['runner_config'])[0]:
                continue
            sc = bridge.adapt(original, preset['runner_config'])
            placements = bridge.schedules(sc, 40, rng)
            inputs.append(dict(scenario=sc.name, encoded=bridge.encode_scenario(sc), schedules=placements))
            if sc.name == 'cancel_waiter_disinherits':
                assert json.loads(json.dumps(evidence.scenario_data(sc))) == preset['cancellation_scenario']
                assert placements == [list(map(tuple, s)) for s in saved[preset['preset'], campaign['seed']]]
        assert hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest() == campaign['all_inputs_sha256']
print('Validated 40 campaign input digests and archived cancellation schedules/configurations/logs')
PY
```

This reconstructs inputs and checks retained artifacts; it does not replay 56,000 executions or validate a kernel binary's build provenance. `runner_sha256` identifies the binary that was actually executed. The archived seed-3 failure preserves its CLI diagnostic and exact schedule; full raw trace evidence for that known case remains in the older M2-01 archives.
