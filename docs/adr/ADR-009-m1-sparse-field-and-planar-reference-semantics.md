# ADR-009: M1 Sparse-Field and Planar Reference Semantics

**Status:** Accepted
**Date:** July 17, 2026
**Amended:** August 2, 2026 ([PR #5](https://github.com/winstanley-industries/apgar/pull/5), [PR #13](https://github.com/winstanley-industries/apgar/pull/13), [PR #6](https://github.com/winstanley-industries/apgar/pull/6))
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
- The established M1 numeric-rule identity remains derived from width,
  clearance, allowed layers, and headings in the Board IR routing profile.
  Compiler and fixture dimensions do not redefine rules. The complete in-memory
  bucket key additionally carries routed-net identity because exact static-
  obstacle interaction depends on ownership. `PreparedRoutingProfile` is the
  Board-IR-issued, source-snapshot-bound capability for selecting that context,
  and `CompiledBoard` retains it.
- CPU A* producer evidence seals the retained `APGAR-ROUTING-PROFILE-V1`
  fingerprint and admits non-default compiled contexts only after the complete
  prepared association is authenticated. Device and replay evidence do not
  yet carry retained-profile identity, so those paths continue to reject
  non-default compiled contexts. A later enabling slice adds the existing
  net-sensitive routing-profile fingerprint; it must not reinterpret the
  existing `APGAR-M1-RULE-BUCKET-V1` scalar. Candidate associations already
  carry that fingerprint, so their
  producer must source that existing field from the retained profile before
  candidate admission can accept a non-default context.
- APGAR has no released serialization compatibility boundary. Before the first
  release, a checked-in V1 durable schema may acquire a missing retained-profile
  field in place only when its schema document, every checked-in producer and
  consumer, and every checked-in artifact change atomically. The explicit
  schema version remains `1`, and old development layouts must fail structural
  validation rather than be guessed or migrated. After the first release, an
  incompatible persistent layout change requires a new major schema version.
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
- Multiple exact compiled contexts may be prepared from one immutable snapshot,
  and CPU A* may route an authenticated retained context. Device, replay, and
  candidate-admission paths remain restricted to the snapshot's default context
  until their own retained-profile identity slices land.
- Users must choose active regions large enough to contain desired planar
  routes. A disconnected sparse field is distinguishable from malformed input
  and unsupported layer transitions.
- Phase 1 does not satisfy the later M1 through-via deliverable by approximation;
  exact via feasibility can extend adjacency after padstack legality exists.
- False-blocked telemetry is meaningful only over represented neighbor edges;
  corpus tooling must separately describe ROI coverage.
- Portals, distance fields, incremental invalidation, GPU layouts, and specialty
  state remain deferred.
