# ADR-012: Deterministic Alternative Policies and Batched Exploration

**Status:** Accepted
**Date:** July 19, 2026
**Applies to:** CPU A*, CUDA candidate generators, and Phase 3 batch execution

## Context

Phase 2 prepares one immutable DeviceCompiledBoard upload but executes one
unpriced route query at a time. Repeating that single-query API would not test
the central Phase 3 hypothesis that the GPU becomes attractive when many
detailed searches share the upload. Diverse alternatives also require bans,
penalties, and objective variations with identical semantics on the CPU oracle
and GPU generators. Mutating global congestion during search would violate the
candidate/allocation separation.

The Phase 2 frontier is a deterministic cost bucket without an admissible
heuristic. Device result headers do not carry query ownership or policy
identity, so they cannot detect cross-query contamination.

## Decision

- CandidateGenerationPolicy v1 is backend-neutral, immutable, request-local,
  normalized, and fingerprinted. It contains objective identity, seed,
  candidate ordinal, recorded orthogonal/diagonal/bend surcharges, sorted
  physical-edge bans, and sorted edge penalties.
- A banned edge is absent. A legal transition cost is the checked sum of the
  compiled step/bend cost, objective surcharges, and that edge's penalty. CPU
  A*, CUDA frontier, CUDA sweep, exact metric reconstruction, differential
  tests, and replay use this one definition.
- Policies cannot modify Board IR, CompiledBoard, resource usage, prices, or
  another query. Unsupported schema/objective/backend combinations fail
  explicitly and may use CPU fallback only with identical semantics.
- The v1 deterministic k-policy schedule retains the base policy first, then
  cycles length surcharge, bend surcharge, one resource penalty, and one
  resource ban with checked deterministic strength/resource selection. Policy
  count and aggregate generated resource entries are each bounded to one
  million.
- DeviceCompiledBoard v1 remains the immutable prepared board upload. A
  separately versioned batch query/result protocol carries query, policy,
  workspace-owner, bounds, completion, and telemetry associations.
- Batch workspaces are disjoint checked query-major slices. One ordinary query
  failure cannot overwrite another query. Device-produced result ownership is
  validated before reconstruction.
- Host preflight validates and classifies each query's ordinal, routing request,
  and normalized policy before backend metadata discovery or upload. A shared
  backend failure applies only to queries that survived preflight; deterministic
  invalid or unsupported peer results are retained.
- Aggregate normalized policy resources are bounded to one million per v1
  input batch. A separate checked logical host-memory budget covers encoded and
  retained query/policy envelopes plus simultaneous flat and partitioned
  readback workspaces. Overflow, configured-budget failure, and host allocation
  failure are explicit `ResourceExhausted` outcomes.
- The frontier evolves into a bounded deterministic heuristic A*-style
  explorer with an admissible policy-aware heuristic and stable
  `(query, f, g, state, heading)` winner key. It does not use a conventional
  global binary heap.
- Heading-aware sweep batches compatible queries against shared directional
  runs. Per-edge bans break propagation and penalties participate in the same
  scalar cost; convergence and round exhaustion are query-specific.
- Compatible batches share Board/compiler/routing/rule/device/generator and
  resource bounds. Results are ordered by unique query identity. Cancellation
  is sampled at bounded launch boundaries and preserves deterministic completed
  versus cancelled outcomes.
- Shared telemetry records dispatched rounds and the optional unfinished-query
  finalization launch independently of untrusted per-query headers. Batch-wide
  launch/readback accounting uses only that shared envelope; a corrupt query
  completion or round value is rejected locally without invalidating peers.
- CPU A* remains the correctness oracle, production default, small-job and
  unsupported-policy fallback until a reproducible end-to-end bakeoff supports
  another dispatch decision.

## Consequences

- One prepared upload can support many semantically identical CPU/GPU policy
  comparisons without policy-specific board uploads.
- Query-major label/predecessor/departure storage increases bounded batch VRAM
  and host readback memory; separate deterministic accounting and smaller-batch
  recovery are required.
- Frontier and sweep may return different equal-cost geometry, but each forced
  backend must be repeatable and match CPU reachability/failure and optimal
  scalar cost under the identical policy.
- The Phase 3 benchmark must compare sequential CPU, parallel host CPU, batched
  frontier, and batched sweep at equal ordered policies and include upload,
  execution, readback, exact admission, and end-to-end costs.
- This decision does not promote a GPU production generator and does not add
  allocator prices, worlds, selection, vias, portals, or legalization.
