# ADR-038: Phase 4 Exact-Small Raw-v2 Authority

**Status:** Accepted for the twenty-fourth Phase 4 vertical slice

**Applies to:** Exact-small fixed-pool oracle publication and frozen decision
protocol authority

## Context

Statistical Decision Protocol v2 correctly moves exact cells to Same-Run Raw
Evidence v2, but it inherits Exact-Small Oracle Artifact v1. The v1 oracle can
bind Raw and per-net authorities but has no durable field for the mandatory
same-run telemetry companion. A nominal v2 wrapper that emits the v1 artifact
would therefore lose an authority required by the decision protocol.

## Decision

- Adopt Exact-Small Oracle Publication v2 as a distinct diagnostic artifact.
- Require Raw-v2, Same-Run Decision Telemetry v1, Wire-v2 Per-Net Report v1,
  and Exact-Small Snapshot v1, validated in that fail-closed order.
- Allow the snapshot producer's existing Raw carrier to select exactly
  Raw-v1/Wire-v1 or Raw-v2/Wire-v2 while preserving the v1 snapshot shape and
  checksum domains.
- Preserve the Oracle-v1 entry point and byte-for-byte Raw-v1 behavior.
- Bind all four authority artifacts and source envelopes in Oracle Artifact v2.
- Keep negative exact-rejection telemetry as valid evidence and keep the
  fixed-pool oracle diagnostic-only.
- Adopt Statistical Decision Protocol v3 as an authority-only supersession that
  replaces Oracle Artifact v1 with v2 in the three exact cells and changes no
  logical cell, disposition, threshold, inference, timing, or guardrail rule.

## Consequences

Every exact-cell oracle can now preserve the complete authority chain required
by the frozen decision protocol. This slice does not prove candidate-pool route
completeness, add missing operational/provenance measurements, aggregate the
matrix, run the experiment, or decide Phase 4.
