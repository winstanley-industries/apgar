# ADR-054: Phase 4 Confirmatory H=4096 Ordinary Per-Net Report Join

**Status:** Accepted for the second post-Protocol-v2 confirmatory implementation slice
**Date:** July 25, 2026
**Applies to:** Development-only H=4096 calibration diagnostics before
same-run, operational, Oracle, or heldout acquisition

## Context

ADR-053 opens only the two Protocol-v2 H=4096 Raw development identities. The
reviewed ordinary `(10200,8)` acquisition is deterministic and complete, but
its candidate arm rejects most requested columns and ends with a board-level
overuse regression. Raw Evidence v1 deliberately cannot attribute those
rejections or publish the complete per-net candidate pools.

Confirmatory Decision Protocol v2 reserves
`phase4_confirmatory_per_net_report_publication_join_v2` for that purpose.
The reserved `v2` name is an authority version, not a payload-schema change.
The existing Corpus-v2 per-net runner, C++ builder, and publication validator
remain H=2250 Protocol-v1 authorities and cannot be widened without making the
two price configurations substitutable.

Per-Net Report Artifact v1 also requires the report and its Raw input to name
the same independently expected clean source commit. The Raw acquired from the
ADR-053 implementation commit therefore remains valid development evidence but
cannot be relabeled or joined to report code committed later.

## Decision

- Keep Per-Net Report Artifact schema 1, Raw Evidence schema 1, Raw Wire 1,
  all checksum domains, complete telemetry payload, and permanent
  `decision_eligible=false` value unchanged.
- Add separately named private H=4096 diagnostic, report-builder, validator,
  runner, and publication-validator entry points. Existing V1 and
  Corpus-v2/H=2250 public entry points remain unchanged and positively reject
  H=4096 semantics.
- Fix the compiled H=4096 runner to Corpus 2, ordinary `(10200,8)`, four
  workers, 20 repetitions, equal-arm `present=1,history=4096`, and canonical
  algorithm-budget checksum `8230401457668518004`. No caller option, case ID,
  wire version, commit, or opaque checksum may select the price authority.
- Require strict argument and complete pure H=4096 spec preflight before source
  checks or any diagnostic execution. Case `10200` is generated, so the
  H=4096 report targets receive no board-fixture path or runfile capability.
- Make the H=4096 test runner permanently preflight-only and unable to emit a
  report, even from a clean stamped checkout. A separate production-shaped
  test target may force only the embedded source gate to an unpublishable
  state. Successful C++ diagnostics are exercised directly without creating a
  publication artifact.
- Add
  `phase4_confirmatory_h4096_per_net_report_validator`. It positively binds
  the reserved Protocol-v2 ordinary per-net authority, then reads and fully
  authenticates the H=4096 ordinary Raw file and exact `(10200,8)` scope before
  opening the bounded regular report file. Only then may it perform the
  complete Corpus-v2 structural Raw/report join.
- After this implementation passes adversarial review and is committed,
  reacquire ordinary Raw `(10200,8)` and generate its report from that exact
  same clean commit. The prior Raw content cannot be copied, relabeled,
  rechecksummed, or accepted under a dual-source exception.
- Keep H=4096 same-run reports, operational measurement, replay, snapshot,
  exact-small Oracle, fixed-query, stress, aggregation, full-matrix, decision,
  heldout, and imported paths closed pending separately reviewed authorities.

## Consequences

One H=4096 calibration Raw artifact can have a canonical independent 64-net
diagnostic companion without changing outcome or timing authority. The report
can classify candidate yield, rejection disposition, pool diversity, and
selected-candidate quality, but it cannot repair, reinterpret, or replace the
Raw board-level outcome.

This slice does not establish route-pool completeness, restore exact
optimality, open the exact-cell report, publish operational evidence, authorize
the confirmatory campaign, decide the matrix, or complete Phase 4.

## Rejected alternatives

- **Join the already reviewed Raw from the prior commit.** The current
  source-envelope contract requires Raw, report, and independently expected
  commit equality; weakening it would require a new dual-source schema.
- **Widen the existing Corpus-v2 report entry points.** They are frozen H=2250
  authorities and must keep rejecting H=4096.
- **Change the report payload schema for the v2 authority.** Protocol v2
  reserves an authority substitution only; the existing payload already
  carries the required complete diagnostics.
- **Give the test runner a fixture or publication escape.** Pure synthetic
  validator fixtures and direct C++ tests cover precommit behavior without a
  second report-acquisition path.
