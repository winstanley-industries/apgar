# ADR-044: Phase 4 Confirmatory Authority Freeze

**Status:** Accepted before confirmatory observation
**Date:** July 23, 2026
**Applies to:** Representative Corpus v2, the Phase 4 V2 evidence campaign,
and all later heldout acquisition and decision publication

## Context

The preserved V1 matrix is a complete authentic negative result. Corpus v2 and
the Plan-v2/Execution-v5/Session-v4 remediation create new semantics that
cannot be published under the V1 manifests or interpreted as an
authority-only amendment to the V1 decision.

Before any V2 heldout allocation outcome is observed, the complete case
identity, workload roster, equal-budget configuration, matrix, analysis rules,
and evidence authority namespace must be frozen.

## Decision

- Freeze Representative Manifest v2 with corpus checksum
  `4182833841936446798`, semantic checksum `9613362670139358355`, 40 paired
  case rows, 38 successful builds, two typed work-bound rows, and 102 canonical
  algorithm-budget entries.
- Freeze Workload-Net Roster Manifest v2 with semantic checksum
  `14986327048461036142`, 38 complete EntityRef rosters, and exclusions for the
  two descriptor-only fixed-query controls and two work-bound stress cases.
- Freeze the V2 aggregate canonical algorithm-budget roster under a new
  checksum domain. Its 102-entry checksum is `15913985307894145139`; individual
  cell checksums retain the existing complete configuration-preimage domain.
- Adopt Confirmatory Decision Protocol v1 with checksum
  `7747512371013753061`. It is a separate campaign rooted in Corpus v2, not a
  supersession or reinterpretation of the negative V1 publication.
- Preserve the V1 lexicographic outcome, family-level exact sign test, Holm
  correction, thresholds, timing rules, and nine completion requirements
  exactly by incorporating Statistical Decision Protocol v4.
- Freeze a 104-cell V2 matrix with the same role arithmetic as V1: 100
  successful evidence cells, 86 noncalibration closure cells, 82 successful
  noncalibration cells, and 78 exact/heldout/imported same-run guardrail cells.
- Require ordinary raw/report/operational authorities for the 22
  calibration/fixed-query/stress raw cells. Require a distinct same-run raw
  authority, authenticated same-invocation exact-rejection telemetry, and
  same-run-specific report and operational joins for the 78
  exact/heldout/imported cells; mixing either authority chain is invalid.
- Keep corpus selection trusted and out of band. Case IDs must never select or
  infer corpus authority. Legacy V1 validators and publishers remain V1-only.
- At this freeze point, no V2 heldout allocation outcome has been executed or
  inspected. Heldout execution requires the clean commit containing these
  authorities and an explicit confirmatory runner path. Premature observation
  invalidates the roster.

## Consequences

The confirmatory evidence implementation can now be built against stable
inputs without moving the analysis target after observation. It still must add
explicit V2 raw, same-run, report, operational, exact-oracle, fixed-query,
stress, runner, and decision-publication paths before heldout acquisition.

This authority freeze does not complete Phase 4. Completion requires a fresh,
complete, clean-commit V2 matrix whose independently rebuilt decision
publication reports both `phase4_exit_status=passed` and
`phase4_complete=true`.
