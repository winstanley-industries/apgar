# ADR-029: Phase 4 Per-Net Arm Telemetry

**Status:** Accepted for the fifteenth Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** In-process per-net diagnostics for successful Phase 4 arms

## Context

Raw Evidence v1 authenticates board-level outcomes, equal opportunity, consumed
route work, isolation, and resource bounds. The Phase 4 exit gate also requires
per-net candidate yield, diversity, and selected-candidate quality. Rebuilding
those facts later from compact arm semantics would lose column classification,
full CandidateMetrics, and proof that a selected candidate belonged to the
authentic final pool. Adding them directly to the existing subprocess wire
would change the already reviewed raw-evidence contract before the reporting
schema and independent validator exist.

## Decision

- Adopt `schemas/benchmark/phase4_per_net_arm_telemetry_v1.md`.
- Add a diagnostic execution seam that derives telemetry while the contender's
  authentic columns, final pools, selected world, and immutable candidates are
  still alive. Return semantics plus telemetry through a type that cannot be
  finalized as a measured arm. Keep normal Paired Trial and Raw Evidence
  execution unchanged.
- Require exactly one full-EntityRef-sorted record, final pool, and selection
  for every authentic workload net, including empty pools and no-candidate
  outcomes. Bind the telemetry to the exact arm semantic checksum and a
  separate domain checksum.
- Close every requested column into admission, duplicate, disconnected,
  unsupported, proof-backed skip, exact-validation rejection, or other
  rejection. Close per-net totals to the arm's existing aggregate semantics.
- Validate selected complete immutable candidate membership in the final pool,
  retain its identity, payload checksum, and all metrics, and close selected
  intrinsic cost to the board outcome.
- Publish pool size, unique geometry/resource signature counts, exact pair
  count, and deterministic integer-ppm mean/minimum resource and geometric
  overlap. Quantize exact CandidateStore integer numerator/denominator ratios
  in checked 128-bit arithmetic with tested half-up boundaries.
- Use widened arithmetic for every partition, sum, and pair calculation and
  fail closed on association, overflow, roster, pool, or selection drift.

## Consequences

The next slice can serialize a report artifact and independently validate its
statistics without rerunning an arm or weakening selection provenance. The
normal measured path pays no diversity-quadratic diagnostic cost and the raw
wire remains v1-compatible.

This telemetry remains candidate-allocation evidence, not exact combined-route
legalization. Phase 5 APGAR DRC and host-CAD validation remain deferred, and no
M1 completion claim follows from this slice.
