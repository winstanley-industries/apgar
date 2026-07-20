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
- Public helpers can construct and update only unsealed diagnostic items. After
  complete untrusted-result validation and reconstruction, the host moves a
  reached result into a separately allocated, truly const evidence snapshot
  bound to the schema and batch identity. Copies share that snapshot and public
  mutation attempts fail. Public batch fields are checked for consistency but
  cannot by themselves manufacture GPU provenance; failure items remain
  unsealed. CUDA candidate provenance also requires explicit preparation by an
  always-linked core check of the exact final CUDA wrapper type. That wrapper's
  construction and delegate binding are private to the CUDA factory; generic
  backends and wrappers cannot acquire producer authentication, including
  wrappers that forward to the real CUDA backend and expose identical metadata.
- Batch workspaces are disjoint checked query-major slices. One ordinary query
  failure cannot overwrite another query. Device-produced result ownership is
  validated before reconstruction.
- Host preflight normalizes every independently valid candidate policy before
  classifying its routing request and compiled endpoint representation, and
  retains that policy identity even when the request is invalid, unsupported,
  off-lattice, or outside the represented sparse field. `input_ordinal` is
  caller correlation only and is independent of the policy's provenance
  `candidate_ordinal`. Preflight occurs before backend metadata discovery or
  upload. Resolved compiled endpoints are carried into device-query encoding
  rather than semantically resolved a second time. A shared backend failure
  applies only to queries that survived preflight; deterministic invalid or
  unsupported peer results are retained. Prepared execution follows the same
  rule: if no query survives preflight, the result is returned without
  inspecting the prepared view, and an unavailable or association-mismatched
  prepared view fails only the admitted survivors.
- Submitted resources from all individually shape-valid policies are summed in
  O(query count) and bounded to one million before any valid policy is copied,
  sorted, iterated, or hashed. Exceeding that work bound is one outer
  `ResourceExhausted` outcome; this is an explicit peer-retention exception
  because deriving policy identities would itself exceed the declared bound.
  Individually invalid shapes remain cheap query-local failures when the valid
  aggregate fits. The pre-normalization host peak is the fixed all-input
  classification/result envelope plus 40 bytes for each such submitted entry.
  Policy normalization compacts owned vectors in place, and compile-time layout
  checks keep both resource-key and penalty records within that 40-byte schema
  bound. This peak is checked before normalization and is not added to the later
  phase peak. Aggregate normalized policy resources are independently
  bounded to one million per v1 device-admitted batch. A separate checked
  logical host-memory budget covers a
  small classification/result envelope for every input plus encoded policies
  and simultaneous flat and partitioned readback workspaces only for queries
  admitted to execution. An allocation-free check proves that the minimum
  all-input classification/result envelope fits before uniqueness, output, or
  admission bookkeeping is allocated. If it does not fit, the API returns one
  outer `ResourceExhausted` because per-query results cannot be retained within
  the declared cap. Once that minimum is affordable, classification precedes
  complete accounting. Aggregate or configured-budget exhaustion marks
  otherwise admitted queries `ResourceExhausted`, preserves already-classified
  invalid or unsupported peers, and prevents backend discovery when no
  executable query remains. Host allocation failure remains an explicit
  `ResourceExhausted` outcome.
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
- GPU `Disconnected` is a negative claim and is never accepted from labels and
  predecessors alone. After structural validation, CPU A* must independently
  return `Disconnected` under the identical normalized policy. A CPU-reachable
  result rejects the GPU output as `gpu.disconnected.cpu_reachable.v1`; an
  oracle failure yields its corresponding non-disconnected structured outcome.
- The public untrusted-result reconstruction seam always normalizes and applies
  the request's complete policy, including bans, penalties, objective
  surcharges, and policy identity. It has no policy-free validation mode.
- Every candidate-batch trust-boundary invariant introduced in Phase 3 has a
  checksummed GPU Candidate-Batch Replay v1 artifact and a replay/test-only
  decorator scenario. This includes the CPU-oracle disconnection check; the six
  workspace, ownership, query/batch telemetry, memory-accounting, and identity
  classes; and rejection of a host-valid CUDA-looking wrapper that lacks
  concrete producer authentication.
  `bazel test --config=cuda //:gpu_batch_replay_test` reproduces all eight exact
  invariant/outcome identifiers without exposing fault controls to production
  generator policy.
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
