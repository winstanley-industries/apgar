# Phase 4 Per-Net Report Publication Join v2

Publication Join v2 binds three independently read canonical files:

1. one complete Same-Run Raw Evidence v2 cell;
2. its complete Same-Run Decision Telemetry v1 companion; and
3. one Per-Net Report Artifact v1 carrying an explicit Wire-v2 association.

Raw v2 is never standalone publication evidence. The validator first performs
the complete Raw-v2/telemetry join with canonical 20-repetition, four-worker,
clean-source, expected-commit, frozen-cell, attempt, process, dispatch,
authority, pair, arm, roster, outcome, and column-partition checks. Only that
success permits the independent per-net report join.

The report remains schema version 1 because its diagnostic payload and checksum
domain are unchanged. Its `raw_wire_schema_version` is exactly 2 and its Raw
source-envelope association uses the Raw-v2 source domain, schema 2, and Wire
2. Wire 1, an unsupported carrier, a Raw-v1 source-envelope domain, or a report
that omits the same-run companion is rejected.

The report is rebuilt and checked exactly as in Publication Join v1. Source,
configuration, corpus, cell-plan, Raw artifact/envelope, repetition-zero
baseline-first pair, both complete arm semantics, and both complete Per-Net Arm
Telemetry objects must structurally equal the corresponding Raw-v2 identities.
The independent frozen EntityRef roster remains authoritative.

Same-run exact-rejection telemetry and the separately rerun rich diagnostic
categories have different authority. A valid nonzero same-run rejection count
is a guardrail result, not corrupt evidence; the diagnostic report cannot
offset, replace, or reinterpret it. The report retains
`decision_eligible=false`, and the join emits no statistics or completion
decision.

All three inputs retain their existing byte and nesting bounds. Production
validation accepts no unstamped, cardinality, worker, or expected-commit
relaxation.
