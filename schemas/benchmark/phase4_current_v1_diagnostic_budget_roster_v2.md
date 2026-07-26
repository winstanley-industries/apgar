# Phase 4 Current-V1 Diagnostic Budget Roster v2

This roster is a test-only, nondecision successor to
[`phase4_current_v1_diagnostic_budget_roster_v1.md`](phase4_current_v1_diagnostic_budget_roster_v1.md).
It freezes the same five Representative Corpus v1 cells after CPU
Candidate-Allocation Session v5 superseded Session v4. It is not a matrix,
publication, statistical-protocol, Corpus-v2, H=4096, or Phase 4 decision
authority.

The canonical JSON contains schema version `2`, Session schema version `5`,
`decision_eligible=false`, an ordered list of `(case_id, pool_size, checksum)`
rows, and a nonzero artifact checksum. Rows remain restricted to cases `100`,
`101`, `102`, `200`, and `4000` at pool size `4`.

The artifact checksum hashes, in order:

1. domain `APGAR-PHASE4-CURRENT-V1-DIAGNOSTIC-BUDGET-ROSTER-V2`;
2. roster schema version as `u32`;
3. Session schema version as `u32`;
4. row count as `u64`;
5. each row's case ID as `u32`, pool size as `u32`, and checksum as `u64`.

Production Raw-v1/Wire-v2 validation continues to use only
`phase4_representative_manifest_v1.json`. This diagnostic roster may be used
only by explicitly nondecision test validation. It cannot satisfy or replace
Representative Manifest v2, canonical algorithm-budget roster v3,
Confirmatory Decision Protocol v2, or any successor acquisition authority.
