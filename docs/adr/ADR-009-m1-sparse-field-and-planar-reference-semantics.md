# ADR-009: M1 Sparse-Field and Planar Reference Semantics

**Status:** Accepted
**Date:** July 17, 2026
**Applies to:** Geometry compiler and deterministic CPU reference router

## Context

The architecture requires conservative sparse directional fields and an exact
CPU reference path, but the current Board IR deliberately has no board outline
or exact via-padstack model. The first compiled-field slice also needs precise
answers for what sparse means, how cross-tile edges are owned, whether diagonal
corner checks may use node occupancy, and what memory/conservatism telemetry
counts.

Allowing the implementation to infer an outline or a permissive layer
transition would silently weaken exactness. Materializing dense placeholders
inside the compilation ROI would also make memory scale with an enclosing box
rather than explicitly requested active work.

## Decision

- CompilerProfile v1 contains an explicit inclusive ROI and an ordered union of
  layer-scoped active regions. The ROI is a compilation bound, not a claimed
  board outline.
- A sparse tile stores only represented nodes in active regions. Missing tiles
  and missing node entries are non-traversable gaps.
- Each represented directed edge is owned by its source node. Cross-tile edges
  use the same mask as intra-tile edges and require a represented destination.
- H/V/45 mask bits are set only after the exact Board IR movement oracle accepts
  the complete adjacent swept envelope. Diagonals are checked as exact diagonal
  capsules; endpoint occupancy is not a substitute for the corner envelope.
- The M1 rule bucket is derived from width, clearance, allowed layers, and
  headings in the Board IR routing profile. Compiler and fixture dimensions do
  not redefine rules.
- CPU A* is planar. Requests keep distinct endpoint-layer fields so exact
  through-via transitions can be added later, but differing layers return a
  structured unsupported result now.
- Search masks and predecessor output are untrusted. Every coalesced returned
  segment is revalidated by exact geometry after reconstruction and association
  hashes/fingerprints are checked before search.
- Edge telemetry is directed. Sparse gaps are outside represented-edge
  conservatism counts. Logical host bytes use the deterministic estimate defined
  in `schemas/compiled_board/v1.md` rather than claiming allocator RSS.

## Consequences

- A compiled-legal edge implies exact legality against every represented static
  obstacle, including at tile boundaries and diagonal corners.
- Users must choose active regions large enough to contain desired planar
  routes. A disconnected sparse field is distinguishable from malformed input
  and unsupported layer transitions.
- Phase 1 does not satisfy the later M1 through-via deliverable by approximation;
  exact via feasibility can extend adjacency after padstack legality exists.
- False-blocked telemetry is meaningful only over represented neighbor edges;
  corpus tooling must separately describe ROI coverage.
- Portals, distance fields, incremental invalidation, GPU layouts, and specialty
  state remain deferred.
