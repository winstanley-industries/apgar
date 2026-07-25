# Phase 4 Confirmatory Decision Protocol v2

This acquisition-free contract is a compact authority-only supersession of
Confirmatory Decision Protocol v1. It binds the inactive H=4096 canonical
configuration without changing the Representative Corpus v2 matrix or any
decision rule and without authorizing execution.

## Authority ancestry

The compact protocol must bind:

- superseded Confirmatory Decision Protocol schema 1 and artifact checksum
  `7747512371013753061`;
- campaign `phase4_confirmatory_corpus_v2`;
- Representative Corpus v2 checksum `4182833841936446798`;
- Representative Manifest v2 checksum `9613362670139358355`;
- Workload-Net Roster Manifest v2 checksum `14986327048461036142`;
- canonical algorithm-budget roster authority
  `phase4_confirmatory_canonical_algorithm_budget_roster_v3`, schema 3, cell
  count 102, and roster checksum `18429170436700418962`; and
- configuration authority `phase4_confirmatory_corpus_v2_h4096`.

The bound configuration is equal in the sequential baseline and
reusable-candidate session:

- `present_step_per_overuse_unit=1`;
- superseded `history_step_per_overuse_unit=2250`;
- effective `history_step_per_overuse_unit=4096`;
- all other canonical algorithm-budget fields unchanged; and
- query, work, and external opportunity unchanged.

Protocol v1, its H=2250 budget authority, and all evidence acquired under that
authority remain stable and cannot be promoted into Protocol v2.

## Unchanged decision contract

The effective Protocol v2 matrix is reconstructed from Protocol v1. Except for
the roster/configuration binding, observation firewall, and exact authority
substitutions below, it must be structurally identical.

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

The protocol preserves the authentic historical V1 negative matrix and does
not reinterpret the incomplete H=2250 confirmatory campaign as a matrix
decision.

## Frozen artifact namespace

Protocol v2 substitutes exactly these ten cell and shared artifact authorities:

1. ordinary Raw outcome and timing:
   `phase4_confirmatory_raw_evidence_v2`;
2. same-run Raw outcome and timing:
   `phase4_confirmatory_same_run_raw_evidence_v2`;
3. same-run exact-rejection guardrail:
   `phase4_confirmatory_same_run_decision_telemetry_v2`;
4. ordinary per-net diagnostic join:
   `phase4_confirmatory_per_net_report_publication_join_v2`;
5. same-run per-net diagnostic join:
   `phase4_confirmatory_same_run_per_net_report_publication_join_v2`;
6. ordinary operational measurement:
   `phase4_confirmatory_operational_measurement_publication_v2`;
7. same-run operational measurement:
   `phase4_confirmatory_same_run_operational_measurement_publication_v2`;
8. exact-small oracle:
   `phase4_confirmatory_exact_small_oracle_v2`;
9. fixed-query control:
   `phase4_confirmatory_fixed_query_control_v2`; and
10. stress evidence:
    `phase4_confirmatory_stress_evidence_v2`.

The complete H=4096 aggregation and decision authority is reserved as
`phase4_confirmatory_matrix_decision_publication_v2`.

An authority suffix does not select or revise a payload or carrier schema.
Ordinary and same-run paths remain distinct, and every later artifact contract
must explicitly bind its Raw Evidence schema, paired-trial wire, telemetry,
report, operational, snapshot, and other payload versions. No v1 confirmatory
artifact authority may appear in the effective v2 matrix.

## Observation firewall

At freeze time no H=4096 allocation outcome has been executed or inspected.
The protocol itself authorizes no acquisition or execution.

The only cells that future separately named and reviewed development entry
points may initially open are:

- exact `(10100,4)` through the same-run authority chain; and
- historical calibration identity `(10200,8)` through the ordinary authority
  chain.

The prior H=2250 observation of `(10200,8)` is context only. It is not H=4096
evidence and cannot satisfy, seed, or be rechecksummed into a v2 authority.
Every other exact or calibration cell remains closed until separately frozen.
Heldout, imported, fixed-query, stress, matrix, and decision execution remain
closed.

Heldout observation additionally requires a future separately reviewed
campaign-acquisition authority. That authority must bind one independently
supplied clean stamped source commit containing the exact Protocol v2 and
canonical roster v3 artifacts plus the complete reviewed execution and
publication chain. The protocol/roster commit by itself is not sufficient.
Corpus version, case ID, cell-plan checksum, wire schema, source commit, or an
opaque matching budget checksum cannot select H=4096. Any premature H=4096
heldout observation invalidates this roster and requires a fresh versioned
authority.

## Serialization

The eventual artifact is compact canonical UTF-8 JSON ending in exactly one
LF. It is bounded, rejects duplicate keys and non-finite numbers, and exactly
reconstructs the frozen literal before accepting its checksum.
`artifact_checksum` uses Board IR v1 FNV-1a domain
`APGAR-PHASE4-CONFIRMATORY-DECISION-PROTOCOL-V2` over every preceding root
field.

The protocol contains no H=4096 outcome, family result, guardrail result,
hardware observation, source commit, acquisition authorization, or completion
decision.
