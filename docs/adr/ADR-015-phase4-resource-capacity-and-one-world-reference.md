# ADR-015: Phase 4 Resource Capacity and One-World CPU Reference

**Status:** Accepted for the first Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Physical-edge capacities, immutable price snapshots,
deterministic one-world candidate selection, and CPU resource accounting

## Context

Phase 3 established immutable, exact-admitted RouteCandidate v1 pools with
canonical physical-edge resource spans. Phase 4 must test the central
board-level hypothesis: selecting combinations of reusable candidates under
shared capacities improves multi-net outcomes. Beginning with GPU worlds or an
allocator microbenchmark would leave the resource units, empty-pool outcome,
association binding, deterministic tie-breaking, and CPU oracle undefined.

The current Board IR v1 implementation also carries one routing profile and
can authentically generate candidates for only that profile's net in one
snapshot. The Phase 4 evidence gate requires distinct nets from a common board
snapshot. That production workload prerequisite is intentionally not hidden by
relabelling same-net alternatives. This slice keeps the allocator independent
of Board IR construction and exercises one authentic nonempty pool plus the
structured empty-pool outcome. Generalizing the canonical multi-net workload
is the next vertical slice before any multi-net or benchmark claim.

## Decision

- Adopt `schemas/allocator/resource_model_v1.md`. V1 resources are the same
  collision-free canonical physical planar edges independently reconstructed
  during RouteCandidate v1 admission. Atomic edge capacity is binary. A model
  supplies one default capacity plus unique overrides; a price snapshot supplies
  one immutable price per named resource and an iteration identity. Both state
  artifacts carry the Board IR/compiler association they were derived for. The
  capacity factory verifies an actual Board Snapshot/Compiled Board pair and
  derives its identity from those artifacts; the price factory inherits that
  identity from the immutable capacity model. Both expose no public mutation
  seam for associations or records.
- Adopt `schemas/allocator/one_world_v1.md`. A request binds all candidates to
  one nonzero Board IR content hash, compiler-profile fingerprint, and compiler
  version. Each net pool must additionally be internally uniform in routing
  profile and rule bucket. Different compatible pools may retain distinct
  profile/bucket associations.
- Canonicalize pool, candidate, capacity, and price order. Reject duplicate
  net, resource, or candidate identities. Never use arrival order, pointer
  identity, or unordered-container iteration as a selection tie-breaker.
- Score cross-policy alternatives by independently reconstructed intrinsic
  base cost times one positive fixed-point weight plus the immutable snapshot's
  resource-price cost. Select the minimum `(score, candidate ID)` tuple.
- Emit exactly one outcome for every requested net pool. A nonempty pool holds
  one immutable selected candidate; an empty pool emits a structured
  no-admissible-candidate outcome.
- Expand selected canonical resource spans on the CPU, accumulate checked
  unsigned usage, apply explicit/default capacity, and report every nonzero
  overuse. Tests independently recompute usage from selected candidate
  footprints.
- Bound net pools, candidates, resource records, and expanded candidate uses
  with request limits no larger than schema hard maxima. Reject arithmetic
  overflow rather than saturating or wrapping costs and usage. Preflight the
  capacity and price factory hard record limits before canonical sorting, then
  enforce their configured combined record budget during allocation. Factory
  lvalue overloads perform any owned record copy inside the structured
  allocation-failure envelope; rvalue overloads preserve ownership transfer,
  and both reject oversized counts before copying or sorting.
- Compute a stable replay checksum over canonical externally visible world
  state. Candidate ID is not payload proof; the world retains the immutable
  candidate handle and its payload checksum. The checksum has a domain-separated
  fixed byte encoding and a golden compatibility vector. Explicit zero prices
  and default-equal capacity overrides remain distinguishable in output.
- Validate candidate span envelopes once before scoring. When prices are
  nonzero, join each compressed candidate span against sorted nonzero price
  records on its canonical resource line; do not expand unpriced edges. Skip
  scoring work entirely when every price is zero. Accumulate selected usage
  with a compressed-span boundary sweep, expanding shared corridors once per
  unique output edge and skipping empty coordinate gaps. Scratch scales with
  span boundaries plus unique output resources. Deterministic counters separate
  logical footprint volume from span queries, price matches, materialized
  accounting edges, and boundary events.

## Consequences

- The allocator can select and account for candidate combinations without
  rerunning pathfinding or depending on Board IR/CAD objects.
- This CPU path is the reference for later price-update, column-generation,
  multi-world, and GPU primitives. GPU work cannot replace it and will require
  differential tests.
- Physical-edge capacity one is useful for the first controlled families but
  is not claimed to predict every exact cross-net conflict. Phase 4 must add
  explicit resource refinement when the capacity model misses an exact
  conflict.
- ADR-016 provides the next versioned authentic multi-net workload from one
  Board IR snapshot. Bounded negotiated price/history updates and targeted
  candidate regeneration remain next; there is still no Phase 4 performance
  or board-level improvement claim.
- Portals, via sites, multi-world execution, GPU allocation, combined exact
  legality, legalization, host validation, and specialty routing remain out of
  this decision.
