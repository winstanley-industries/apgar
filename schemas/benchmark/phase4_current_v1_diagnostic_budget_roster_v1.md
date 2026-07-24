# Phase 4 Current-V1 Diagnostic Budget Roster v1

This roster freezes the five Representative Corpus v1 cells exercised by
cross-process diagnostic tests after CPU Candidate-Allocation Session v4
superseded the decision run's Session-v3 authority. It is not a matrix,
publication, statistical-protocol, or Phase 4 decision authority.

The canonical JSON contains schema version `1`, Session schema version `4`,
`decision_eligible=false`, an ordered list of `(case_id, pool_size, checksum)`
rows, and a nonzero artifact checksum. Rows cover only cases `100`, `101`,
`102`, `200`, and `4000` at pool size `4`.

The artifact checksum hashes, in order:

1. domain `APGAR-PHASE4-CURRENT-V1-DIAGNOSTIC-BUDGET-ROSTER-V1`;
2. roster schema version as `u32`;
3. Session schema version as `u32`;
4. row count as `u64`;
5. each row's case ID as `u32`, pool size as `u32`, and checksum as `u64`.

Production Raw-v1/Wire-v2 validation continues to use only
`phase4_representative_manifest_v1.json`. This diagnostic roster may be used
only by explicitly nondecision test/diagnostic validation; it cannot satisfy or
replace the frozen Phase 4 V1 publication protocol.
