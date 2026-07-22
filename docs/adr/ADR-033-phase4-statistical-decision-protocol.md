# ADR-033: Phase 4 Statistical Decision Protocol

**Status:** Accepted for the nineteenth Phase 4 vertical slice
**Date:** July 21, 2026
**Applies to:** Predeclared Phase 4 matrix, inference, guardrails, and timing diagnostics

## Context

Raw and diagnostic schemas define trustworthy per-cell facts but do not freeze
how 104 logical cells become a family-level Phase 4 decision. Choosing samples,
guardrails, or multiplicity treatment after seeing outcomes would make the
global-allocation claim irreproducible. A failed evidence attempt must also not
be laundered into an allocator loss.

## Decision

- Adopt `schemas/benchmark/phase4_statistical_decision_protocol_v1.md` and its
  compact canonical JSON artifact.
- Bind the current representative manifest, its complete successful roster,
  the two descriptor-only fixed-query controls, and the two work-bound stress
  tiers. Freeze exact 104-cell and 86-cell closure expansions.
- Use held-out cases at K=8 as family experimental units. Apply exact one-sided
  sign tests with ties discarded and deterministic exact-rational Holm-
  Bonferroni correction. Require two qualified families including the globally
  coupled pin-field family; sensitivity pools cannot rescue the primary result.
- Freeze lexicographic board outcome order and per-cell selected-net, imported,
  cost, and same-run exact-rejection guardrails with no pooling offsets.
- Publish descriptive outward-rounded Clopper-Pearson bounds and order-balanced
  paired timing diagnostics, but never use timing to decide Phase 4.
- Keep missing or failed evidence incomplete. Require exact-small, fixed-query,
  stress, same-run telemetry, full operational telemetry, and provenance before
  completion.

## Consequences

Future evidence aggregation has a deterministic, machine-validated decision
contract that cannot be rewritten after results are observed. This slice does
not contain observed results, name a winner, establish telemetry coverage, or
complete Phase 4.
