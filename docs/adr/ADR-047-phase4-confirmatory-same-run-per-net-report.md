# ADR-047: Phase 4 Confirmatory Same-Run Per-Net Report Join

**Status:** Accepted for the third post-freeze confirmatory implementation slice
**Date:** July 23, 2026
**Applies to:** Corpus v2 exact-development diagnostics before heldout acquisition

## Context

ADR-046 opens only the ordinary Corpus v2 `(10200,4)` Per-Net Report
publication path. Exact, heldout, and imported cells use Same-Run Raw Evidence
v2 plus a separately authenticated Same-Run Decision Telemetry companion, so
their diagnostic publication authority is a three-file join. The legacy
Publication Join v2 and its Python entry points remain Corpus v1 authority.

## Decision

- Keep Per-Net Report Artifact v1, its checksum domains, and permanent
  `decision_eligible=false` value unchanged.
- Add separate production and test-only same-run confirmatory report runners.
  Both require explicit Corpus 2 and Wire 2 and accept exactly development cell
  `(10100,4)` before fixture resolution, representative-case construction, or
  diagnostic execution. The ordinary confirmatory runner remains restricted to
  `(10200,4)` and Wire 1.
- Reuse the explicit Corpus v2 spec, diagnostic, report builder, roster, budget,
  and source-envelope entry points already reviewed under ADR-046.
- Add a separately named Corpus v2 three-way publication validator. It reads
  and completely validates Raw first, rejects every cell except `(10100,4)`,
  then reads and joins the bounded regular same-run telemetry sidecar. Only
  after that complete join may it open and structurally join the bounded
  regular report.
- Preserve all Corpus v1 Python entry points and the legacy
  `validate_phase4_per_net_report_v2.py` behavior unchanged.
- Exercise a real 20-repetition same-run acquisition, deterministic diagnostic
  reruns, complete Raw/sidecar/report joins, cross-authority rejection,
  fail-closed runner scope, clean-source enforcement, and nonblocking
  Raw-before-sidecar-before-report input ordering.

## Consequences

One exact-development Corpus v2 cell now proves the same-run diagnostic
publication path without observing a heldout outcome. Raw remains authoritative
for allocation outcome and timing. The sidecar remains authoritative for the
exact-rejection guardrail, including an authentic failed guardrail. The rich
diagnostic rerun cannot replace or reinterpret either authority.

Other exact cells, every heldout and imported cell, operational measurement,
oracle publication, aggregation, campaign execution, and the Phase 4 decision
remain closed.
