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
  cost, metrics, physical-edge resources, canonical form, signatures,
  checksum, and memory accounting.
- CompiledBoard and generator/GPU results may reject or guide work but cannot
  prove admission legality. Every exact rejection retains a versioned
  structured diagnostic.
- Physical resources v1 are collision-free structured canonical compiled-edge
  spans. Reverse traversal names the same capacity unit. This is deliberately
  narrower than the future allocator resource vocabulary.
- Geometry and resource signatures are versioned 128-bit lookup accelerators.
  Signature matches always receive canonical equality checks before duplicate
  classification.
- The store enforces positive per-net candidate-count and logical-byte budgets,
  deterministic ranking/enumeration/pruning, resource and geometry diversity,
  and metric dominance. Immutable candidates are separate from mutable store
  metadata.
- CAN-002 is represented by owner-scoped retention pins. A pinned candidate is
  never pruned; Phase 3 does not define worlds, selection, congestion, or
  prices.
- Concurrent batch admission is serialized at publication and uses stable
  total keys. Externally visible results do not depend on worker scheduling,
  pointer identity, or unordered-container iteration.

## Consequences

- A candidate can be reproduced, compared, and bounded without trusting the
  generator that produced it.
- Exact validation and resource reconstruction add visible end-to-end cost;
  Phase 3 benchmarks must report that cost separately from GPU execution.
- Candidate IDs, signatures, and checksums have distinct roles. None may be
  used alone as collision-proof equality or legality evidence.
- Resource spans are sufficient for the Phase 3 diversity experiment but do
  not implement Phase 4 allocator capacities, portals, prices, worlds, or
  column generation.
- The reserved via tag preserves format evolution while keeping exact
  through-via routing an explicit remaining M1 gap.

