## Problem and result

Describe the concrete problem and resulting behavior. Link the issue or M2 work item. State any remaining limitation.

## Validation

List commands/configurations and their results, including skipped, failed or unrun checks. Link relevant artifacts. For a behavior change, identify the cases or analysis that can detect a violation of the contract.

## Review checklist

- [ ] Each authored commit has my matching `Signed-off-by` trailer (`git commit -s`); DCO is separate from cryptographic signing.
- [ ] Scope and implementation limits are reflected in the user/API documentation and Project status where applicable.
- [ ] Contract changes update requirements/specifications; architecture decisions have an ADR. Explain N/A in Validation.
- [ ] Kernel semantics changes update the reference model and conformance evidence in the same PR. Explain N/A for other changes.
- [ ] User-visible changes have an Unreleased changelog entry, or Validation explains why none is needed.
- [ ] Required checks pass, review conversations are resolved, and known failures are disclosed before merge.

