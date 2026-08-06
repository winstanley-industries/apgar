# ADR-072: Single Bounded Targeted-Regeneration Epoch

**Status:** Accepted for P4R-07

**Date:** August 6, 2026

**Applies to:** EPIC-001 P4R-07 deterministic CPU targeted-regeneration execution

## Context

P4R-06 returns one immutable price snapshot, canonical hot-resource order, and
canonical regeneration-target order over explicit immutable P4R-03 pools. It
does not execute a route query, admit or publish a column, or refresh selection.
P4R-07 must close exactly that execution gap without introducing the reusable
session or repeated-epoch state reserved for P4R-08.

The execution boundary must use production CPU A*, authenticated candidate
construction, exact admission, CAN-004 batch publication, P4R-03 selection, and
P4R-02B accounting. Reconstructing candidates, publishing target-by-target, or
mixing CS-RR-v1 query-local costs into allocator prices would create a second
authority or make caller order and partial work observable. A fatal error after
mutating an externally owned store would also make it impossible to return
neither partial publication nor a partial result.

## Decision

P4R-07 defines `ExecuteCpuTargetedRegenerationEpoch` as one synchronous,
failure-atomic CPU composition over an immutable Board IR snapshot, resource
capacity model, complete immutable source pools, the exact immutable P4R-06
plan derived from those pools, an optional exact prior P4R-06 price snapshot,
and one prepared compiler/request context for every source-pool net.

### Plan and source authentication

- The execution receives the exact P4R-06 planning configuration and optional
  prior snapshot used to create the plan. P4R-06 replay validation checks the
  plan, snapshot, target, pool-roster, configuration, and prior-chain identity
  bindings without rerunning P4R-03.
- Board, capacity, plan, price, source-pool, prepared-context, request/net, and
  common-resource-lattice associations are checked before any CPU query.
- Contexts and source pools are canonicalized by Board IR `EntityRef`. Exactly
  one context and one pool are required for every plan net. Caller order,
  pointer address, and container allocation are not semantic.
- The source-pool candidate roster and candidate identities must be exactly the
  roster bound by the P4R-06 plan. Source candidates remain immutable.

### Exact target and price policy

- Targets execute exactly once and in the canonical order already published by
  P4R-06. P4R-07 neither adds targets nor loops, retries, or composes epochs.
- Every target produces exactly one attempted CPU column. A no-target plan
  produces zero attempted columns and still completes the publication/refresh
  boundary over its exact source pools.
- The caller's per-net request supplies endpoints, layer, intrinsic objective,
  deterministic seed, candidate ordinal, and scalar surcharges. Caller-authored
  resource bans or penalties are rejected; P4R-06 is the sole resource-price
  authority for this epoch.
- The query policy contains every P4R-06 snapshot entry whose physical edge
  exists in the target's retained prepared view, with
  `additional_cost = total_price`. Snapshot entries absent from that view are
  untraversable and are omitted. The policy is normalized by the Phase 3
  candidate-policy authority before routing.
- Production CPU A* receives the normalized immutable policy and a deterministic
  work limit. A reached route passes only through authenticated CPU candidate
  construction and exact admission. No candidate field or producer evidence is
  fabricated.
- Disconnected and currently unsupported routes, ordinary candidate-build or
  exact-admission rejection, duplicate identity/geometry/resources, and
  deterministic retention rejection are canonical per-column outcomes.
  Work exhaustion, host resource exhaustion, association failure, arithmetic
  failure, invalid authenticated source state, and internal invariants are
  fatal and return no epoch result.

### One atomic publication and one refresh

- Every immutable source candidate enters as an owning `StoredCandidate`
  handle from prior exact admission. The original authenticated
  `RouteCandidate` object is copied through that handle rather than rebuilt
  from its public generated payload. Those incumbents are staged with every
  authentic generated draft and every canonical pre-admission rejection in
  one fresh local `CandidateStore` incumbent-plus-mixed-draft batch.
- That single CAN-004 transaction performs exact admission, diagnostics,
  duplicate handling, deterministic retention, and publication across all
  nets. Target or worker completion cannot create a separate store mutation.
- The store is local and cannot escape before final success. Any fatal error,
  including one detected after its local batch commit, destroys the store and
  exposes neither partial publication nor a partial epoch result.
- Complete immutable post-publication pools are rebuilt from the local store in
  canonical net and CandidateStore rank order. Exactly one
  `SelectOneWorldZeroPrice` call then produces the refreshed P4R-03 selection
  and its P4R-02B accounting. There is no pre-publication P4R-03 rerun.

### Result, bounds, and precedence

- Success returns one move-only immutable epoch value containing the immutable
  input plan, canonical columns and rejection diagnostics, complete owned
  refreshed pools, exactly one refreshed selection/accounting value, checked
  counters, and stable batch and epoch identities.
- The batch identity binds the plan, price snapshot, source roster, prepared
  request values, normalized per-target price policies, and execution
  configuration. The epoch identity additionally binds every column outcome,
  complete rejection value, refreshed pool/candidate identity, refreshed
  selection/accounting, and aggregate counters. These hashes are deterministic
  replay bindings, not hostile-operator attestations.
- Hard and configured bounds cover nets, source candidates and bytes, targets,
  price-projection visits and entries, route queries, per-query and aggregate
  CPU work, per-column and aggregate generated bytes, transaction items/input
  bytes/work, retained candidate bytes, rejection records, refreshed P4R-03
  pools/candidates/resource expansion, and all result counters. Equality is
  accepted; the first value above a bound fails.
- Arithmetic is checked before allocation or state advance. Per-net source and
  price bounds are checked in canonical net order; target work and aggregate
  result bounds are checked in canonical P4R-06 target order.
- Fatal precedence is configuration; Board/capacity and non-allocating
  roster/target bounds; plan replay; detailed source-pool and context checks;
  price-policy projection and static bounds; canonical target generation;
  aggregate generated bounds; the one local store transaction and result
  correlation; refreshed-pool rebuilding; exactly one refreshed P4R-03
  selection/accounting; final identity assembly. An ordinary earlier column
  diagnostic never masks a later fatal error.

## Independent exact-small reference

Exact-small tests do not call the P4R-07 coordinator. They independently
project the P4R-06 prices, invoke production CPU A* and authenticated candidate
construction, exact-admit authentic drafts, model canonical duplicate and
bounded publication behavior over source plus generated candidates, select the
lexicographic P4R-03 winner directly, expand `PhysicalEdgeSpan` values into
atomic `EdgeResourceKey` uses, and recompute usage and overuse. Production
P4R-07 selection, P4R-02B accounting, and epoch identity helpers are not used by
the oracle.

## Consequences

- P4R-07 supplies one observable column-generation epoch and no reusable
  allocator state. Repeating it requires an explicit caller action and does not
  constitute the P4R-08 composed contender.
- A generated route may be authentic and exactly legal yet publish as a
  duplicate or lose deterministic retention; both remain successful bounded
  epoch diagnostics rather than fabricated progress.
- Atomically rebuilding from owning exact-admitted incumbents makes the
  returned pools owned and self-contained while preserving the one-transaction
  publication boundary and producer authentication.
- The implementation, exact-small oracle, and adversarial failure/bound matrix
  are one cohesive acceptance slice. If the task crosses EPIC-001's 2,500-line
  extra-scrutiny threshold, splitting the oracle or transaction tests would
  leave the runnable epoch self-confirming or its fatal atomicity unproved.

## Explicit non-goals

This decision does not compose or loop epochs; begin P4R-08; reuse CS-RR-v1
costs; add Multi-World or GPU allocation; create a reusable store or allocation
session; classify campaign readiness; or add serialized evidence schemas,
runners, telemetry, operational capture, readiness, campaign, or heldout
machinery. It does not legalize or commit selected routes to a CAD board.
