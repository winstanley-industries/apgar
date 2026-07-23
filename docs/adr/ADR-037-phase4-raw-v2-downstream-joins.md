# ADR-037: Phase 4 Raw-v2 Downstream Diagnostic Joins

**Status:** Accepted for the twenty-third Phase 4 vertical slice

**Applies to:** Per-net diagnostic publication and operational projection for
same-run decision cells

## Context

Statistical Decision Protocol v2 assigns exact, held-out, and imported cells to
Same-Run Raw Evidence v2 plus its telemetry companion. The existing per-net
publication validator and operational projector admit only standalone Raw v1,
so those 78 cells cannot reach later aggregation without version-aware joins.

## Decision

- Adopt `phase4_per_net_report_publication_join_v2.md` and
  `phase4_operational_projection_v2.md`.
- Keep Per-Net Report Artifact v1 and its diagnostic payload/checksum domain.
  Its existing Raw carrier field now admits exactly the version-matched
  Raw-v1/Wire-v1 and Raw-v2/Wire-v2 source envelopes.
- Require every v2 downstream consumer to validate the complete Raw-v2 and
  Same-Run Decision Telemetry authority before reading it as a successful
  publication input.
- Preserve the v1 validation entry points and byte-for-byte v1 output.
- Bind, but do not reinterpret, same-run rejection authority in the v2
  operational projection. Negative guardrail evidence remains valid evidence.
- Keep all standalone-decision, coverage, and telemetry-completion flags false.

## Consequences

Raw-v2 decision cells now have authenticated per-net and operational joins
without promoting independently rerun diagnostics into same-run authority.
This slice does not upgrade exact-small oracle publication, add missing
operational/provenance measurements, aggregate the matrix, or decide Phase 4.
