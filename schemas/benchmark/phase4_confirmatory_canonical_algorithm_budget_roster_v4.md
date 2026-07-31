# Phase 4 Confirmatory Canonical Algorithm-Budget Roster v4

**Status:** Frozen and implemented as an acquisition-free configuration
authority; acquisition is not authorized by this document.

Roster v4 is the acquisition-free configuration successor to
`phase4_confirmatory_canonical_algorithm_budget_roster_v3`. It freezes the
complete Session-v5/H=4096 canonical algorithm-budget namespace without
changing a payload schema, authorizing execution, or observing an allocation
result.

## Authority ancestry

The checked-in compact canonical JSON uses these exact root fields in this
exact order:

1. `schema_version`;
2. `authority`;
3. `corpus_version`;
4. `corpus_checksum`;
5. `representative_manifest_schema_version`;
6. `representative_manifest_checksum`;
7. `workload_roster_manifest_schema_version`;
8. `workload_roster_manifest_checksum`;
9. `supersedes`;
10. `configuration_authority`;
11. `configuration`;
12. `cell_count`;
13. `roster_checksum`; and
14. `canonical_algorithm_budgets`.

The fixed root values are:

- `schema_version=4`;
- `authority=phase4_confirmatory_canonical_algorithm_budget_roster_v4`;
- Representative Corpus v2 and checksum `4182833841936446798`;
- Representative Manifest schema 2 and checksum `9613362670139358355`;
- Workload-Net Roster Manifest schema 2 and checksum
  `14986327048461036142`;
- configuration authority
  `phase4_confirmatory_corpus_v2_h4096_session_v5`; and
- `cell_count=102`; and
- `roster_checksum=12316700735749461907`.

`supersedes` is an object with these exact fields and order:

1. `schema_version=3`;
2. `authority=phase4_confirmatory_canonical_algorithm_budget_roster_v3`;
3. `roster_checksum=18429170436700418962`; and
4. `configuration_authority=phase4_confirmatory_corpus_v2_h4096`.

The predecessor JSON, checksum, generator stdout, validation rules, and all
102 per-cell checksums remain unchanged.

## Configuration transition

`configuration` is an object with these exact fields and order:

1. `present_step_per_overuse_unit=1`;
2. `history_step_per_overuse_unit=4096`;
3. `baseline_and_candidate_price_configs_equal=true`;
4. `superseded_candidate_allocation_session_schema_version=4`;
5. `candidate_allocation_session_schema_version=5`;
6. `superseded_targeted_regeneration_plan_schema_version=2`;
7. `targeted_regeneration_plan_schema_version=3`;
8. `superseded_targeted_regeneration_execution_schema_version=5`;
9. `targeted_regeneration_execution_schema_version=6`;
10. `all_non_session_canonical_algorithm_budget_fields_unchanged=true`;
11. `all_candidate_session_fields_except_schema_version_unchanged=true`; and
12. `query_work_and_external_opportunity_unchanged=true`.

For every canonical cell, the successor spec must equal the roster-v3
H=4096/Session-v4 spec after changing exactly
`candidate_session_config.schema_version` from the explicit fixed v4 constant
to the explicit fixed v5 constant. Baseline, preparation, prices, limits,
seeds, schedules, stopping depth, target and column bounds, route limits,
query/work opportunity, and external budgets must compare field-for-field
equal.

The per-cell checksum domain remains
`APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-V1`. It hashes the complete baseline,
preparation, and candidate-session configuration and therefore directly binds
Session v5. Plan v3 and Execution v6 have no separate fields in
`Phase4PairedTrialSpec`; their numeric transition is authenticated by the
aggregate roster and normatively composed by the Session-v5 contract.

## Canonical cell roster

The `canonical_algorithm_budgets` shape and order are identical to roster v3:
40 increasing-case rows containing exactly 102 increasing `(case_id,pool)`
cells. The cells are the three exact, 18 calibration, 72 heldout, three
executable fixed-query, three stress, and three imported canonical budget
cells embedded in Representative Manifest v2. Descriptor-only `(12000,1024)`
and `(12001,1)` remain excluded.

Every row has exact fields `case_id`, then `pool_checksums`. Every pool entry
has exact fields `pool`, then `checksum`. Missing, duplicate, extra, reordered,
zero-checksum, descriptor-only, predecessor-checksum, or foreign cells are
invalid.

The compact UTF-8 JSON must contain no BOM or duplicate key, use strict JSON
integer and boolean types, contain no non-finite number, and end in exactly one
LF. Re-serialization with `ensure_ascii=false`, `allow_nan=false`, and compact
separators must reproduce the exact bytes.

## Roster checksum

`roster_checksum` uses Board IR v1 FNV-1a with this exact typed preimage:

1. `AddString("APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-ROSTER-V4")`;
2. `AddU32(schema_version)`;
3. `AddString(authority)`;
4. `AddU32(corpus_version)`, then `AddU64(corpus_checksum)`;
5. `AddU32(representative_manifest_schema_version)`, then
   `AddU64(representative_manifest_checksum)`;
6. `AddU32(workload_roster_manifest_schema_version)`, then
   `AddU64(workload_roster_manifest_checksum)`;
7. `AddU32(supersedes.schema_version)`;
8. `AddString(supersedes.authority)`;
9. `AddU64(supersedes.roster_checksum)`;
10. `AddString(supersedes.configuration_authority)`;
11. `AddString(configuration_authority)`;
12. `AddU64(configuration.present_step_per_overuse_unit)`;
13. `AddU64(configuration.history_step_per_overuse_unit)`;
14. `AddBool(configuration.baseline_and_candidate_price_configs_equal)`;
15. `AddU32(configuration.superseded_candidate_allocation_session_schema_version)`;
16. `AddU32(configuration.candidate_allocation_session_schema_version)`;
17. `AddU32(configuration.superseded_targeted_regeneration_plan_schema_version)`;
18. `AddU32(configuration.targeted_regeneration_plan_schema_version)`;
19. `AddU32(configuration.superseded_targeted_regeneration_execution_schema_version)`;
20. `AddU32(configuration.targeted_regeneration_execution_schema_version)`;
21. `AddBool(configuration.all_non_session_canonical_algorithm_budget_fields_unchanged)`;
22. `AddBool(configuration.all_candidate_session_fields_except_schema_version_unchanged)`;
23. `AddBool(configuration.query_work_and_external_opportunity_unchanged)`;
24. `AddU64(cell_count)`; and
25. for every cell in increasing case/pool order, `AddU32(case_id)`,
    `AddU32(pool)`, and `AddU64(checksum)`.

Strings use the Board IR stable-hash length-prefixed byte encoding; booleans
use its single-byte encoding. Validation constrains every schema version,
case, pool, and `cell_count` to the u32 range even though `cell_count` retains
the listed u64 checksum encoding.

## Builder and capability boundary

The fixed private builder must:

1. reconstruct the frozen roster-v3 H=4096/Session-v4 preimage;
2. reject unless its candidate-session schema equals the explicit Session-v4
   constant;
3. replace only that field with the explicit Session-v5 constant; and
4. return the separately named successor preimage.

It must not use a moving current-version alias, become public API, change a
frozen builder, or appear in an execution-authority switch. The no-argument
generator must use only the dead-section-eliminated configuration-preimage
graph. Ordinary and sanitizer-reset `llvm-nm` inspection must prove that its
exact final binary contains no fixture, representative-case, preparer, worker,
CandidateStore, CPU A-star, routing, or allocator-execution capability.

## Authority closure

Roster v4 is configuration authority only. It authorizes no fixture access,
case construction, preparer or worker creation, allocator query, Raw evidence,
same-run telemetry, report, operational measurement, exact snapshot or oracle,
fixed-query, stress, imported, heldout, matrix, aggregation, decision,
publication, or acquisition.

Confirmatory Decision Protocol v2 remains byte-frozen and bound to roster v3.
ADR-062 and
`phase4_confirmatory_decision_protocol_v3.md` activate the separately reviewed
acquisition-free Protocol-v3 authority binding roster v4. Separately reviewed
development acquisition and consuming-publication authorities must bind the
complete successor chain from one clean stamped source commit before either
designated development cell may run. ADR-063 freezes the inactive Raw
successor boundary, but its preflight and validators alone do not satisfy that
complete-chain condition or authorize fixture-backed execution.
