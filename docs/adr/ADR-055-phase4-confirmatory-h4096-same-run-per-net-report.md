# ADR-055: Phase 4 Confirmatory H=4096 Same-Run Per-Net Report Join

**Status:** Accepted for the third post-Protocol-v2 confirmatory implementation slice
**Date:** July 25, 2026
**Applies to:** Development-only H=4096 exact-cell diagnostics before
operational, Oracle, or heldout acquisition

## Context

ADR-053 opened the exact H=4096 `(10100,4)` Raw-v2/Wire-2 acquisition and its
Same-Run Decision Telemetry companion. The reviewed development acquisition
passed the exact-rejection guardrail and reduced, but did not eliminate,
board-level overuse. Raw and same-run telemetry deliberately do not publish the
complete independently regenerated candidate pools needed to attribute route
yield and selection quality.

Confirmatory Decision Protocol v2 reserves
`phase4_confirmatory_same_run_per_net_report_publication_join_v2` for that
diagnostic. The reserved `v2` name is an authority version, not a report
payload-schema change. Existing Corpus-v2/H=2250 same-run report entry points
remain Protocol-v1 authorities and cannot be widened.

The exact Raw and telemetry acquired before this report implementation also
name an earlier clean source commit. Per-Net Report Artifact v1 requires Raw,
report, and the independently expected commit to match. The three-way
publication validator additionally requires the Raw and same-run telemetry to
share that source. Earlier artifacts therefore cannot be relabeled or joined
to this implementation.

## Decision

- Keep Same-Run Raw Evidence schema 2, Raw Wire 2, Same-Run Decision Telemetry
  schema 1, Per-Net Report Artifact schema 1, all checksum domains, complete
  telemetry payloads, and permanent `decision_eligible=false` unchanged.
- Add separately named private H=4096 same-run diagnostic, report-builder,
  validator, runner, and publication-validator entry points. Existing ordinary
  H=4096, V1, and Corpus-v2/H=2250 entry points remain unchanged and positively
  reject cross-configuration semantics.
- Fix the compiled H=4096 same-run report runner to Corpus 2, exact
  `(10100,4)`, four workers, 20 repetitions, Raw Wire 2, equal-arm
  `present=1,history=4096`, and canonical algorithm-budget checksum
  `8829615204625848656`. No caller field or opaque checksum may select the
  price authority.
- Require strict argument and complete pure H=4096 same-run spec preflight
  before source checks or diagnostic execution. Case `10100` is generated, so
  H=4096 same-run report targets receive no board-fixture path or fixture
  runfile capability.
- Make the H=4096 same-run test runner permanently preflight-only and unable to
  emit a report. A separate production-shaped test target may force only the
  embedded source gate to an unpublishable state. Successful diagnostics are
  exercised directly in C++ without creating a publication artifact.
- Add
  `phase4_confirmatory_h4096_same_run_per_net_report_validator`. It positively
  binds the reserved Protocol-v2 same-run per-net authority, then:
  1. reads and fully authenticates H=4096 Raw-v2/Wire-2 `(10100,4)`;
  2. reads and fully joins its bounded regular H=4096 same-run telemetry
     companion; and
  3. only then opens the bounded regular report and performs the complete
     Corpus-v2 structural report join.
- Keep the report and sidecar independently joined to Raw. They need no new
  direct shared field. A valid nonzero exact-rejection count remains failed
  guardrail evidence, not malformed evidence, and the rich report cannot
  offset or reinterpret it.
- After this implementation passes adversarial review and is committed,
  reacquire exact Raw `(10100,4)`, its telemetry sidecar, and its report from
  that exact same clean commit. Prior artifact content cannot be copied,
  relabeled, rechecksummed, or accepted under a dual-source exception.
- Keep H=4096 operational measurement, replay, snapshot, exact-small Oracle,
  fixed-query, stress, aggregation, full-matrix, decision, heldout, and
  imported paths closed pending separately reviewed authorities.

## Consequences

The exact development cell can have a canonical six-net diagnostic companion
while Raw remains allocation outcome/timing authority and same-run telemetry
remains exact-rejection guardrail authority. The report can attribute yield,
rejection disposition, retained diversity, and selected-candidate quality. It
cannot establish route-pool completeness, global feasibility, or exact
optimality.

This slice does not publish operational evidence, open the exact-small Oracle,
authorize the confirmatory campaign, decide the matrix, or complete Phase 4.

## Rejected alternatives

- **Join the already reviewed exact artifacts.** Their source commit predates
  this report implementation and cannot satisfy the same-source contract.
- **Widen the existing H=2250 same-run entry points.** That would make the
  frozen H=2250 and H=4096 price authorities substitutable.
- **Treat the report as a guardrail repair.** Exact rejection authority remains
  exclusively in the same-run telemetry companion.
- **Give the test runner a fixture or publication escape.** Pure synthetic
  validator fixtures and direct C++ diagnostics cover precommit behavior
  without creating a second acquisition path.
