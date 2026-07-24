# ADR-045: Phase 4 Confirmatory Raw Authority

**Status:** Accepted for the first post-freeze confirmatory implementation slice
**Date:** July 23, 2026
**Applies to:** Corpus v2 isolated execution, Raw evidence, and same-run
decision telemetry before heldout acquisition

## Context

ADR-044 froze the Corpus v2 manifests and confirmatory protocol before any V2
heldout outcome was observed. The existing isolated controller, subprocess
wire, Raw serializers, and validators were deliberately Corpus v1 authorities.
Inferring a corpus from case IDs or widening those public V1 entry points would
make cross-corpus evidence substitution possible.

The first executable V2 slice must prove that the frozen exact and calibration
cases can pass through the complete process and validation boundary without
unlocking heldout acquisition.

## Decision

- Add separate Corpus v2 entry points for canonical spec construction, arm
  execution and reconciliation, isolated worker/controller execution,
  external-authority finalization, paired assembly, and same-run capture
  validation.
- Retain Wire v1 for ordinary Raw and Wire v2 for atomic same-run Raw plus
  telemetry, but select corpus authority out of band at encode and decode.
  Legacy wire entry points continue to reject Corpus v2 semantics.
- Add a separate `phase4_confirmatory_evidence_runner`. Its production build
  requires explicit `--corpus_version=2`, a clean stamped source commit, and a
  frozen exact or calibration descriptor. A test-only build may relax source
  stamping and repetition cardinality; the production binary has no such
  escape.
- Add separate confirmatory Raw and same-run validator entry points and CLI
  targets. They authenticate Representative Manifest v2, Workload-Net Roster
  Manifest v2, the Corpus v2 checksum/root/cell-plan/budget domains, and the
  Confirmatory Decision Protocol v1 scope. The legacy validators remain
  Corpus v1-only.
- Exercise one exact same-run cell and one calibration ordinary cell in tests,
  require V1/V2 cross-authority rejection, and require the development runner
  to reject heldout cases before worker launch.
- Do not execute fixed-query, stress, heldout, or imported confirmatory cases
  in this slice. A later reviewed and committed campaign runner must
  authenticate the frozen clean commit and exact protocol role before those
  cases can run.

## Consequences

Corpus v2 now has an executable and independently validated development path
for the two pre-observation roles that cannot reveal heldout outcomes. This
allows later report, operational, oracle, control, and aggregation authorities
to be built and adversarially reviewed against real V2 artifacts while the
observation firewall remains intact.

This slice does not begin the confirmatory campaign and does not complete Phase
4. Heldout acquisition remains forbidden until the complete acquisition and
publication chain is reviewed and committed. Phase 4 still requires a complete
clean-commit V2 matrix whose final publication reports both
`phase4_exit_status=passed` and `phase4_complete=true`.
