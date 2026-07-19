# ADR-011: Phase 3 Candidate Identity, Admission, and Store

**Status:** Accepted
**Date:** July 19, 2026
**Applies to:** RouteCandidate v1, exact admission, rejection diagnostics, and
deterministic candidate storage

## Context

Phase 2 returns exactly validated planar routes but has no durable candidate
contract. Letting generator or GPU output become a stored route directly would
make compiled masks, self-reported costs, resource hashes, and implementation
object layouts de facto legality and serialization authorities. The global
allocator planned for Phase 4 also requires immutable reusable candidates,
bounded storage, collision-safe deduplication, and a retention seam before any
world state exists.

Board IR v1 does not yet contain exact via templates or via-padstack checking.
Candidate geometry nevertheless needs a stable primitive union that can add
through-via validation later without silently accepting invented transitions
now.

## Decision

- RouteCandidate v1 is defined first by
  `schemas/candidate/route_candidate_v1.md`. It carries a stable 128-bit ID,
  exact net and terminal references, independent Board/compiler/routing/rule
  associations, complete deterministic generator policy/provenance, canonical
  exact geometry, compressed physical-edge resources, reconstructed metrics,
  constraints, versioned signatures, checksum, and deterministic logical-byte
  accounting.
- Geometry v1 supports ordered exact line segments and reserves a tagged
  through-via primitive. Admission returns `Unsupported` for every through-via
  primitive until exact via-template compilation and validation exist. Arcs,
  branches, multipin trees, and arbitrary angles are incompatible with v1.
- Candidate admission follows generated, normalized, exact-validated,
  resource-accounted, signed/deduplicated, and stored stages. It independently
  verifies intended terminals, ordering/connectivity, signed coordinates,
  H/V/45 headings, exact swept Board IR clearance, associations, policy scalar
  cost, policy-independent intrinsic base cost, metrics, physical-edge
  resources, canonical form, signatures, checksum, and memory accounting.
- CompiledBoard and generator/GPU results may reject or guide work but cannot
  prove admission legality. Every exact rejection retains a versioned
  structured diagnostic.
- Physical resources v1 are collision-free structured canonical compiled-edge
  spans with unit usage. Reverse traversal names the same capacity unit, and a
  candidate that reuses a physical edge is rejected as noncanonical. Phase 3
  costs are nonnegative, so this removes useless loops without losing a
  shortest-path alternative. This is deliberately narrower than the future
  allocator resource vocabulary.
- Canonical line geometry visits each same-layer path vertex once and permits
  only consecutive lines to share their ordered endpoint. Every nonconsecutive
  same-layer swept trace pair requires exact centerline distance at least the
  nominal trace width; equality is legal. Crossings, point self-touches, and
  sub-width separation fail exact admission. Positive-length collinear overlap
  remains classified later as repeated physical-edge resource use.
- Exact self-clearance uses a deterministic x-bound sweep with a v1 maximum of
  `1,000,000` broad-phase pair inspections per admission. Candidate pairs count
  before layer, adjacency, and y-bound filtering once reached by the sweep;
  attempting the next pair fails with structured `BudgetExhausted` evidence.
  The hostile-test seam may only reduce this production cap.
- Geometry and resource signatures are versioned 128-bit lookup accelerators.
  Signature matches always receive canonical equality checks before duplicate
  classification.
- The store enforces positive per-net candidate-count and logical-byte budgets,
  deterministic ranking/enumeration/pruning, resource and geometry diversity,
  and metric dominance. Immutable candidates are separate from mutable store
  metadata. A store instance binds to one complete Board/compiler/routing/rule
  association set and rejects association drift rather than mixing stale and
  current candidates under the same net identity.
- CAN-002 is represented by owner-scoped retention pins. A pinned candidate is
  never pruned; Phase 3 does not define worlds, selection, congestion, or
  prices.
- Concurrent generation publishes through one explicit batch, which is sorted
  and serialized using stable total keys. Batch results do not depend on worker
  completion order, pointer identity, or unordered-container iteration.
  Separate single-item calls are race-safe and linearizable, but bounded
  retention is intentionally not specified as commutative across unknown
  future calls; schedule-independent callers must form a batch.

## Consequences

- A candidate can be reproduced, compared, and bounded without trusting the
  generator that produced it.
- Exact validation and resource reconstruction add visible end-to-end cost;
  Phase 3 benchmarks must report that cost separately from GPU execution.
- Candidate IDs, signatures, and checksums have distinct roles. None may be
  used alone as collision-proof equality or legality evidence.
- Candidate ranking and Pareto dominance compare the independently
  reconstructed intrinsic base cost and quality vector. Scalar policy costs
  remain authoritative for identical-policy CPU/GPU differential checks but
  are not compared across alternative objective identities.
- Resource spans are sufficient for the Phase 3 diversity experiment but do
  not implement Phase 4 allocator capacities, portals, prices, worlds, or
  column generation.
- The reserved via tag preserves format evolution while keeping exact
  through-via routing an explicit remaining M1 gap.
