# Changelog and release checklist

The repository is in `0.2` development and has no recorded release tags. Version constants in `include/emb/version.h` are not proof of a published or supported version. This page defines the release process; it does not cut a release or imply that the future evidence pipeline is implemented. The [roadmap](../architecture/07-roadmap.md), [Project status](../STATUS.md), [repository workflow](repository-workflow.md) and [timing/evidence contract](timing-and-evidence.md) determine what can be claimed.

## Maintain the changelog

Edit the root [CHANGELOG.md](https://github.com/EmbLinkOs/EmbLinkRTOS/blob/main/CHANGELOG.md) in the same PR as a user-visible change. Write observable behavior, API/configuration compatibility and a migration instruction when needed. Use Added, Changed, Fixed, Removed and Known limits as applicable. Do not put a fix under Fixed while the reproduction is still unexplained or failing. Keep entries in Unreleased until the exact release commit is approved and validated.

At release time move only delivered entries into `## [version] - YYYY-MM-DD`, using the actual release date and an immutable tag/source link. Keep unfinished entries in Unreleased. Avoid claiming an Accepted specification is an implemented feature. A versioned support statement must name the actual supported profile, board, compiler versions and maintenance dates.

## Scope and source checkpoint

- [ ] Choose the version and release scope: development API maturity, supported profiles/targets/compiler versions, experimental features and excluded designs.
- [ ] Inspect all previous known failures and the [M2 checklist](m2-checklist.md). Required unexplained failures block a release; exclusions need explicit scope and rationale.
- [ ] For 1.0, complete the roadmap's M2 core and M3a primary Cortex-M controller/evidence gates. Native/QEMU outcomes do not substitute for the physical controller.
- [ ] Review any API/format changes and migration notes; do not freeze the unimplemented isolation ABI or hardware schema.
- [ ] Set and verify the numeric version in the public header and CMake project together. Record any prerelease label separately where the current numeric version API cannot express it.
- [ ] Identify one clean, reviewed source SHA and its exact configuration/toolchain inputs. Confirm the three required CI checks on that revision and preserve links/artifacts.

## Validation and artifacts

- [ ] Run applicable checked/release, native/emulated/physical-target conformance. Record individual cases, assertions, skips, disabled/unrun cases and reasons with configuration/source identity.
- [ ] Run the reference model and required differential campaigns. Classify and regress the known deep replay failures; record seeds, encoded inputs and exact schedules.
- [ ] Produce applicable requirement-to-executed-case/analysis evidence and coverage reports with declared gaps. Header ID references and design catalog extraction do not establish coverage.
- [ ] Complete applicable static analysis and documented deviations. Format/banned-construct checks alone do not satisfy the future full analysis gate.
- [ ] Build the documentation strictly and run its integrity checks; validate examples against the release configuration, including migration instructions.
- [ ] Measure physical timing for advertised bounds and record total flash/RAM/stack, instrumentation overhead and interference assumptions. Label maximum observed separately from an established bound.
- [ ] Record stress/soak, boundary/resource-limit and failure-recovery evidence for the supported application scope.
- [ ] Produce build manifests, dependency/tool versions, licence notices and an SBOM. Preserve exact input/output identities; the current build does not generate the complete release bundle automatically.
- [ ] Demonstrate the required double-build reproducibility with pinned tools and defined build-identity treatment; preserve the comparison/attestation.
- [ ] Assemble source archive, selected reference artifacts, docs, evidence, release/migration notes and checksums tied to the same SHA. Authenticate the checksum manifest along with the release identity.

REL-001 requires a signed release, SBOM and reproducibility/conformance evidence. These obligations are not waived by creating a GitHub release or calling a build a preview. Until the bundle/gates exist, CI artifacts and the Pages site are development snapshots with disclosed limits. No new release-automation or signing key is provisioned by this documentation task.

## Support and publication

- [ ] Declare security support start/end dates, fix/backport policy, supported toolchains/targets and maintainer capacity for the release line; update SECURITY.md. Do not promise an unfunded LTS cadence.
- [ ] Complete the maintainer release review: scope, unresolved exclusions, migration, licences, evidence and checksums all agree.
- [ ] Create and verify a signed annotated tag at the reviewed SHA using the maintainer's existing signing identity. DCO trailers and ordinary unsigned tags do not satisfy cryptographic release signing.
- [ ] Push the verified tag and publish a GitHub release with matching notes/artifacts/checksums. There is currently no automated release workflow; do this only as the scoped release action, not as part of routine documentation setup.
- [ ] Publish the matching reviewed documentation revision with the manual Documentation workflow. The current site is a single development snapshot, not a version switcher or immutable release archive; archive release docs with the bundle.
- [ ] Re-download and verify the public artifacts, tag/signature and checksums, and check the published scope/support/migration links.

Example signing commands, only after the checklist is satisfied and a signing key is already configured:

```sh
git tag -s vX.Y.Z <reviewed-source-sha> -F <reviewed-release-notes-file>
git verify-tag vX.Y.Z
git push origin vX.Y.Z
```

Use the actual version, SHA and notes file; never generate a maintainer signing key or bypass verification merely to make these commands run. Record evidence that was not executed as unrun/blocked, then complete it before advertising the corresponding guarantee.

