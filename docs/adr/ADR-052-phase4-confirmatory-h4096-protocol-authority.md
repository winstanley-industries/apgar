# ADR-052: Phase 4 Confirmatory H=4096 Protocol Authority

**Status:** Accepted for the eighth post-freeze confirmatory implementation slice
**Date:** July 25, 2026
**Applies to:** Acquisition-free confirmatory protocol supersession for the frozen H=4096 configuration

## Context

ADR-050 records that the H=2250 production result for exact development cell
`(10100,4)` did not equal the fixed-pool exhaustive optimum, so the required
exact-small Oracle Artifact could not be published. ADR-051 therefore freezes
an inactive H=4096 configuration hypothesis under canonical algorithm-budget
roster v3 without changing Representative Corpus v2, its case and workload
authorities, or any H=2250 artifact.

Confirmatory Decision Protocol v1 checksum-binds the H=2250 roster and its
artifact authority namespace. Changing either in place would reinterpret the
existing development evidence and the authentic historical V1 matrix.
Conversely, the H=4096 roster alone cannot select an execution path or make an
artifact publishable. A separate protocol authority is required before any
configuration-specific runner or validator is implemented.

The protocol freeze and the eventual acquisition source commit are also
different authorities. This documentation-only slice cannot bind a source
commit containing execution and publication entry points that have not yet
been reviewed or committed.

## Decision

- Preserve Confirmatory Decision Protocol v1 byte-for-byte and adopt
  Confirmatory Decision Protocol v2 as a compact authority-only supersession of
  schema 1 artifact checksum `7747512371013753061`. The supersession does not
  reinterpret H=2250 evidence or the authentic negative V1 decision.
- Retain campaign `phase4_confirmatory_corpus_v2` and bind configuration
  authority `phase4_confirmatory_corpus_v2_h4096` through
  `phase4_confirmatory_canonical_algorithm_budget_roster_v3`, schema 3, cell
  count 102, and roster checksum `18429170436700418962`.
- Bind the complete configuration transition: equal-arm
  `present_step_per_overuse_unit=1`, superseded
  `history_step_per_overuse_unit=2250`, and effective
  `history_step_per_overuse_unit=4096`. Baseline and reusable-candidate price
  configurations remain equal; every other canonical algorithm-budget field
  and every query, work, and external opportunity remain unchanged.
- Reconstruct the effective protocol from Protocol v1 and change only its
  configuration/roster authority, observation firewall, and artifact authority
  namespace. The 104 logical cells, 100 successful evidence cells, 86
  noncalibration closure cells, 82 successful noncalibration cells, role and
  evidence dispositions, families, board outcome, inference, guardrails,
  timing, and nine completion requirements remain unchanged. Descriptor-only
  cells `(12000,1024)` and `(12001,1)` remain outside the 102-cell budget
  roster.
- Reserve exactly these H=4096 artifact authorities:
  `phase4_confirmatory_raw_evidence_v2`,
  `phase4_confirmatory_same_run_raw_evidence_v2`,
  `phase4_confirmatory_same_run_decision_telemetry_v2`,
  `phase4_confirmatory_per_net_report_publication_join_v2`,
  `phase4_confirmatory_same_run_per_net_report_publication_join_v2`,
  `phase4_confirmatory_operational_measurement_publication_v2`,
  `phase4_confirmatory_same_run_operational_measurement_publication_v2`,
  `phase4_confirmatory_exact_small_oracle_v2`,
  `phase4_confirmatory_fixed_query_control_v2`, and
  `phase4_confirmatory_stress_evidence_v2`. Reserve
  `phase4_confirmatory_matrix_decision_publication_v2` as the only H=4096
  complete-decision publication authority.
- Treat those suffixes as authority versions only. They do not implicitly
  change a Raw Evidence, paired-trial wire, telemetry, report, capture,
  snapshot, or other payload schema. Every later contract must select and bind
  its carrier and payload versions explicitly.
- Freeze the only initial cells that future separately reviewed development
  entry points may open: exact cell `(10100,4)` through the same-run carrier and
  calibration cell `(10200,8)` through the ordinary carrier. The latter reuses
  only the identity of the historical H=2250 calibration cell; its old evidence
  is not H=4096 evidence and cannot be promoted or rechecksummed.
- Keep every H=4096 execution and acquisition path closed in this slice.
  Protocol v2 alone authorizes no fixture access, case construction, preparer,
  worker, replay, Raw artifact, downstream publication, or Oracle execution.
  Heldout, imported, fixed-query, stress, full-matrix, and decision paths remain
  closed.
- Require a future separately frozen and adversarially reviewed
  campaign-acquisition authority before heldout observation. It must bind one
  clean stamped source commit containing the exact Protocol v2 and roster v3
  authorities together with the completed reviewed execution and publication
  chain. A commit containing only the protocol and roster is insufficient.
  Premature H=4096 heldout observation invalidates the roster and requires a new
  versioned authority.

## Consequences

The repository can freeze the complete H=4096 decision contract and namespace
without executing an allocator or creating evidence. Later exact and
calibration execution paths can be implemented as separate narrow slices
without widening any H=2250 entry point.

This protocol does not establish that H=4096 restores exact optimality, does
not publish an outcome, does not authorize heldout acquisition, and does not
complete Phase 4. H=2250 artifacts remain valid only under their original
authorities.

## Rejected alternatives

- **Rewrite Protocol v1 or accept its artifacts under v2 names.** This would
  erase the configuration boundary and reinterpret authentic evidence.
- **Infer H=4096 from Corpus v2, a case ID, wire version, source commit, or
  opaque budget checksum.** Configuration selection must remain explicit and
  out of band.
- **Use the protocol/roster freeze commit as the heldout acquisition commit.**
  That commit cannot contain the later reviewed execution and publication
  chain.
- **Open all development roles immediately.** Each execution and validation
  entry point requires its own adversarially reviewed slice and exact cell
  boundary.
