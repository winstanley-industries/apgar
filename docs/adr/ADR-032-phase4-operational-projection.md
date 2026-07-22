# ADR-032: Phase 4 Operational Projection

**Status:** Accepted for the eighteenth Phase 4 vertical slice
**Date:** July 21, 2026
**Applies to:** Operational views derived from canonical Raw Evidence v1

## Context

Raw Evidence v1 already authenticates isolated elapsed time, route-work and
candidate accounting, persistent process lifecycle, and one `wait4` resident
peak per contender process. The Phase 4 report still needs an arm-centric view
of those facts without weakening Raw, inventing unavailable utilization or
cache measurements, or making one successful cell look like complete evidence.

## Decision

- Adopt `schemas/benchmark/phase4_operational_projection_v1.md`.
- Require production projection and validation to begin with complete Raw v1
  publication validation and an independent expected commit.
- Rebuild the complete projection from the returned Raw document. Bind every
  source, configuration, corpus, cell, environment, authority, controller,
  pair, arm, semantic, record, and external-authority checksum.
- Publish checked timing residuals, work and candidate-accounting splits,
  process/order/lifecycle identity, and exactly one lifetime peak per process.
- Use explicit unavailable or not-applicable tags for every measurement Raw v1
  cannot support. Keep the CPU-only direct-precompiled execution annotation
  separate from any measured cache claim.
- Mark the Raw cell as eligible input to statistics while fixing standalone
  decision eligibility, coverage, and Phase 4 telemetry completeness to false.
- Keep Raw Evidence v1 and Wire v1 unchanged and defer aggregation, statistics,
  matrix coverage, and the Phase 4 decision.

## Consequences

Downstream statistics can consume a canonical operational view without
reinterpreting Raw scopes or losing low-level authentication ancestry. The
projection does not add stage measurements or resource authority. Missing
utilization, cache, detailed cold-stage, and per-repetition RSS data remain
visible and must be supplied by a later separately versioned instrumented
measurement path if the Phase 4 report requires them.
