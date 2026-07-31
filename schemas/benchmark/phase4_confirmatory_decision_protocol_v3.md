# Phase 4 Confirmatory Decision Protocol v3

**Status:** Frozen and implemented as an active acquisition-free protocol
authority; execution and acquisition are not authorized by this document.

This acquisition-free contract is a compact authority-only supersession of
Confirmatory Decision Protocol v2. It binds the Session-v5/H=4096 canonical
configuration without changing the Representative Corpus v2 matrix or any
decision rule and without reinterpreting the observed Session-v4/H=4096
development evidence.

## Root shape and authority ancestry

The checked-in compact canonical JSON uses these exact root fields in this
exact order:

1. `schema_version`;
2. `supersedes`;
3. `campaign`;
4. `protocol_state`;
5. `canonical_algorithm_budget_authority`;
6. `observation_firewall`;
7. `development_execution_plan`;
8. `artifact_authority_namespace`;
9. `matrix_authority_counts`;
10. `unchanged_v1_sections`; and
11. `artifact_checksum`.

The fixed root schema version is `3`. `supersedes` has exact fields
`schema_version=2`, then
`artifact_checksum=11520586171987743043`, binding the checked-in canonical
Confirmatory Decision Protocol v2. Protocol v2, canonical roster v3, their
reserved authorities, and all evidence acquired under them remain stable and
cannot be promoted into Protocol v3.

`campaign` has these exact fields and order:

1. `campaign_id=phase4_confirmatory_corpus_v2`;
2. `configuration_authority` equal to
   `phase4_confirmatory_corpus_v2_h4096_session_v5`;
3. `preserves_v1_negative_matrix=true`;
4. `preserves_v2_development_observations=true`; and
5. `does_not_supersede_v1_decision=true`.

The protocol binds unchanged Representative Corpus v2 checksum
`4182833841936446798`, Representative Manifest v2 checksum
`9613362670139358355`, and Workload-Net Roster Manifest v2 checksum
`14986327048461036142` through canonical algorithm-budget roster v4.

## Protocol state

`protocol_state` has these exact fields and order:

1. `protocol_frozen=true`;
2. `decision_not_evaluated=true`;
3. `heldout_outcomes_observed=false`;
4. `protocol_v2_development_outcomes_observed=true`;
5. `session_v5_h4096_allocation_outcomes_observed=false`;
6. `publishable_session_v5_h4096_evidence_created=false`;
7. `authority_only_supersession=true`;
8. `protocol_alone_authorizes_execution=false`; and
9. `protocol_alone_authorizes_acquisition=false`.

The predecessor observation field records the two authentic development
bundles governed by Protocol v2 and ADR-059. It does not import their outcome,
source commit, timing population, or artifact bytes into Protocol v3. Fresh
Session-v5 state fields are scoped to the successor configuration and must not
repeat Protocol v2's now-historical global statement that no H=4096 allocation
outcome existed.

## Canonical algorithm-budget binding

`canonical_algorithm_budget_authority` has these exact fields and order:

1. `authority`;
2. `schema_version`;
3. `corpus_version`;
4. `corpus_checksum`;
5. `representative_manifest_schema_version`;
6. `representative_manifest_checksum`;
7. `workload_roster_manifest_schema_version`;
8. `workload_roster_manifest_checksum`;
9. `superseded_roster_schema_version`;
10. `superseded_roster_authority`;
11. `superseded_roster_checksum`;
12. `superseded_configuration_authority`;
13. `configuration_authority`;
14. `configuration`;
15. `cell_count`; and
16. `roster_checksum`.

It exactly binds:

- authority
  `phase4_confirmatory_canonical_algorithm_budget_roster_v4`;
- schema version `4`, cell count `102`, and roster checksum
  `12316700735749461907`;
- corpus and manifest versions and checksums from roster v4;
- superseded roster schema `3`, authority
  `phase4_confirmatory_canonical_algorithm_budget_roster_v3`, checksum
  `18429170436700418962`, and configuration authority
  `phase4_confirmatory_corpus_v2_h4096`; and
- configuration authority
  `phase4_confirmatory_corpus_v2_h4096_session_v5`.

The nested `configuration` object is byte-for-byte equal in field order and
value to roster v4:

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

## Unchanged decision contract

The effective Protocol v3 matrix is reconstructed from Protocol v2. Except for
the roster/configuration binding, successor-scoped observation firewall, and
exact artifact-authority substitutions below, it must be structurally
identical.

It retains:

- 104 unique logical cells;
- 100 confirmatory Raw success cells;
- 86 noncalibration closure cells;
- 82 successful noncalibration cells;
- 22 ordinary and 78 same-run successful cells;
- all role, case/pool, evidence-disposition, closure, family, and globally
  coupled-family assignments;
- the board-level lexicographic outcome, exact family sign test, Holm
  correction, guardrail thresholds, diagnostic timing rules, and all nine
  completion requirements; and
- descriptor-only exclusions `(12000,1024)` and `(12001,1)`, which are not
  canonical algorithm-budget cells.

`matrix_authority_counts` and `unchanged_v1_sections` are exactly equal in
field order and value to Protocol v2.

`matrix_authority_counts` contains, in order:

1. `logical_cell_count=104`;
2. `confirmatory_raw_success_cell_count=100`;
3. `noncalibration_closure_cell_count=86`;
4. `noncalibration_confirmatory_raw_success_cell_count=82`;
5. `ordinary_raw_success_cell_count=22`;
6. `same_run_raw_success_cell_count=78`; and
7. `same_run_guardrail_cell_count=78`.

`unchanged_v1_sections` is the ordered array:

1. `corpus_case_and_workload_authorities`;
2. `logical_matrix`;
3. `cell_roles_and_evidence_dispositions`;
4. `families`;
5. `base_decision_contract`;
6. `outcome`;
7. `inference`;
8. `guardrail_thresholds`;
9. `timing_statistics`; and
10. `completion_requirements`.

The protocol preserves the authentic historical V1 negative matrix and the
two Protocol-v2 development observations without treating either as a
Session-v5 result or a Protocol-v3 matrix decision.

## Frozen artifact namespace

Protocol v3 substitutes exactly these ten configuration-specific authorities:

1. `ordinary_raw_outcome_and_timing`:
   `phase4_confirmatory_raw_evidence_v2` to
   `phase4_confirmatory_raw_evidence_v3`;
2. `same_run_raw_outcome_and_timing`:
   `phase4_confirmatory_same_run_raw_evidence_v2` to
   `phase4_confirmatory_same_run_raw_evidence_v3`;
3. `same_run_exact_rejection_guardrail`:
   `phase4_confirmatory_same_run_decision_telemetry_v2` to
   `phase4_confirmatory_same_run_decision_telemetry_v3`;
4. `ordinary_per_net_diagnostic_join`:
   `phase4_confirmatory_per_net_report_publication_join_v2` to
   `phase4_confirmatory_per_net_report_publication_join_v3`;
5. `same_run_per_net_diagnostic_join`:
   `phase4_confirmatory_same_run_per_net_report_publication_join_v2` to
   `phase4_confirmatory_same_run_per_net_report_publication_join_v3`;
6. `ordinary_operational_measurement`:
   `phase4_confirmatory_operational_measurement_publication_v2` to
   `phase4_confirmatory_operational_measurement_publication_v3`;
7. `same_run_operational_measurement`:
   `phase4_confirmatory_same_run_operational_measurement_publication_v2` to
   `phase4_confirmatory_same_run_operational_measurement_publication_v3`;
8. `exact_small_oracle`:
   `phase4_confirmatory_exact_small_oracle_v2` to
   `phase4_confirmatory_exact_small_oracle_v3`;
9. `fixed_query_control`:
   `phase4_confirmatory_fixed_query_control_v2` to
   `phase4_confirmatory_fixed_query_control_v3`; and
10. `stress_evidence`:
    `phase4_confirmatory_stress_evidence_v2` to
    `phase4_confirmatory_stress_evidence_v3`.

Each substitution row has exact fields `purpose`, `supersedes`, then
`authority`, and retains the Protocol-v2 purpose and order. The complete
Session-v5/H=4096 aggregation and decision authority is reserved as
`phase4_confirmatory_matrix_decision_publication_v3`, superseding
`phase4_confirmatory_matrix_decision_publication_v2`.

`artifact_authority_namespace` has these exact fields and order:

1. `configuration_specific=true`;
2. `authority_version_does_not_imply_payload_schema_change=true`;
3. `substitutions`, containing the ten rows above;
4. `matrix_decision_publication_supersedes`, equal to
   `phase4_confirmatory_matrix_decision_publication_v2`; and
5. `matrix_decision_publication`, equal to
   `phase4_confirmatory_matrix_decision_publication_v3`.

An authority suffix does not select or revise a payload or carrier schema.
Ordinary and same-run paths remain distinct, and every later consuming
contract must explicitly bind its Raw Evidence schema, paired-trial wire,
telemetry, report, operational, snapshot, and other payload versions. No
Protocol-v2 artifact authority may appear in the effective Protocol-v3 matrix.

## Observation firewall and development scope

The protocol itself authorizes no fixture access, case construction, preparer,
worker, allocator query, replay, artifact, publication, execution, or
acquisition.

The only cells that future separately named and reviewed Session-v5
development entry points may initially open are:

- exact `(10100,4)` through paired-trial Wire 2 and
  `phase4_confirmatory_same_run_raw_evidence_v3`; and
- calibration `(10200,8)` through paired-trial Wire 1 and
  `phase4_confirmatory_raw_evidence_v3`.

The case IDs, pool sizes, roles, carriers, and wire schemas remain equal to
Protocol v2. Only their configuration-specific Raw authorities advance from
v2 to v3. Every other exact or calibration cell remains closed. Heldout,
imported, fixed-query, stress, matrix, aggregation, and decision execution
remain closed.

The exact observation-firewall arrays remain equal to Protocol v2.
`observation_firewall` has these exact fields and order:

1. `development_case_ids`, equal to
   `[10100,10101,10102,10200,10201,10210,10211,10220,10221]`;
2. `heldout_case_ids`, equal in order to `11000` through `11007`, `11100`
   through `11107`, then `11200` through `11207`;
3. `heldout_execution_closed=true`;
4. `future_campaign_acquisition_authority_required=true`;
5. `future_authority_must_bind_clean_stamped_source_commit=true`;
6. `future_authority_must_bind_protocol_v3_and_roster_v4=true`;
7. `future_authority_must_bind_complete_execution_and_publication_chain=true`;
8. `future_publication_source_commit_must_equal_acquisition_commit=true`;
9. `protocol_and_roster_commit_alone_sufficient_for_heldout=false`;
10. `cross_configuration_evidence_relabeling_forbidden=true`;
11. `protocol_v2_evidence_cannot_satisfy_protocol_v3=true`; and
12. `premature_session_v5_h4096_heldout_observation_invalidates_roster=true`.

`development_execution_plan` has these exact fields and order:

1. `protocol_freeze_includes_entrypoints=false`;
2. `separately_reviewed_session_v5_h4096_entrypoints_required=true`;
3. `initial_development_cells_are_complete_scope=true`;
4. `initial_development_cells`, containing the exact two ordered rows below;
   and
5. `closed_execution_roles`, equal to
   `["heldout","imported","fixed_query","stress","matrix","decision"]`.

Each initial-development row has exact fields `case_id`,
`requested_pool_size`, `role`, `carrier`, `raw_wire_schema_version`, then
`raw_authority`. The rows are:

1. case `10100`, pool `4`, role `exact`, carrier `same_run`, Raw Wire schema
   `2`, and authority `phase4_confirmatory_same_run_raw_evidence_v3`; and
2. case `10200`, pool `8`, role `calibration`, carrier `ordinary`, Raw Wire
   schema `1`, and authority `phase4_confirmatory_raw_evidence_v3`.

A protocol/roster-only commit is insufficient for heldout execution. Corpus
version, case ID, cell-plan checksum, wire schema, source commit, or an opaque
matching budget checksum cannot select the Session-v5 configuration.

## Serialization

The checked-in artifact is compact canonical UTF-8 JSON ending in exactly one
LF. It is bounded, rejects duplicate keys, type aliases, non-finite numbers,
unknown or reordered fields, and exactly reconstructs the frozen literal
before accepting its checksum. It is 6,303 bytes including the final LF, has
`artifact_checksum=4963299999381388941`, and has SHA-256
`c3812719674fdf6958379272fbb3b2af4439ca12cdc2532b5dda230097dd473d`.

`artifact_checksum` uses Board IR v1 FNV-1a by adding domain string
`APGAR-PHASE4-CONFIRMATORY-DECISION-PROTOCOL-V3`, then the compact canonical
JSON string of every preceding root field in order. The strict validator first
validates checked-in Protocol v2 and roster v4, reconstructs the exact
successor, proves the matrix and decision sections unchanged, and rejects
rechecksummed ancestry, namespace, state, or firewall forgeries.

The protocol contains no Session-v5 allocation outcome, family result,
guardrail result, hardware observation, source commit, acquisition
authorization, or completion decision.

ADR-063 freezes the inactive Session-v5/H=4096 Raw successor boundary.
Acquisition-free preflight and validator implementation under that contract
does not satisfy the required complete execution and consuming-publication
chain, authorize fixture-backed execution, or change any field of this
canonical artifact.
