# ADR-071: Bounded Negotiated-Price and Regeneration Planning

**Status:** Accepted for P4R-06

**Date:** August 5, 2026

**Applies to:** EPIC-001 P4R-06 deterministic CPU price update and targeted-regeneration planning

## Context

P4R-02B established canonical physical-edge capacity and accounting. P4R-03
selects one candidate or structured empty-pool absence per net and recomputes
usage over immutable pools. P4R-04 prepares those pools, while P4R-05 provides
the separate CS-RR-v1 sequential comparison arm. Phase 4 now needs the smallest
candidate-allocator step that converts one P4R-03 selection into recorded
negotiated prices and a deterministic request for later column generation.

That step must not execute regeneration. It also cannot borrow CS-RR-v1's
query-local congestion costs: those costs are calculated around sequential
incumbent removal and are implementation state of the comparison arm, not
candidate-allocator state. Unspecified price timing, historical retention,
hotset ordering, empty-pool behavior, target priority, identity, or work bounds
would make a later regeneration epoch non-replayable.

## Decision

P4R-06 defines `PlanNegotiatedRegeneration` as one pure, failure-atomic planning
boundary over a Board IR snapshot, the existing immutable
`ResourceCapacityModel`, explicit immutable `OneWorldCandidatePool` values, and
an optional prior P4R-06 price snapshot.

### Selection and association

- The planner calls P4R-03 `SelectOneWorldZeroPrice`; it does not reproduce or
  replace P4R-03 ranking or P4R-02B accounting.
- Board, capacity, candidate, pool/net, candidate-ID, and accounting failures
  retain their typed underlying selection/accounting codes. Caller order,
  pointer identity, and repeated execution are not semantic.
- A prior snapshot must match the resource lattice, binary capacity, and exact
  P4R-06 price policy. Its canonical entries, checked totals, epoch factor,
  schedule-reachable present and historical components, replay-chain shape,
  and stable identity are validated before reuse. Reconstructed current and
  cumulative per-resource overuse must fit the configured selection/accounting
  caps for one epoch and all preceding epochs.

### Exact price schedule

Epoch zero has no prior snapshot. A valid prior epoch `e - 1` advances exactly
to epoch `e`; callers cannot skip or relabel an epoch. For policy constants
`initial_present_factor`, `present_factor_increment`, and
`historical_price_increment`:

```text
present_factor(e) = initial_present_factor
                  + e * present_factor_increment

present_price(e, r) = present_factor(e) * overuse(e, r)

historical_price(e, r) = historical_price(e - 1, r)
                       + historical_price_increment * overuse(e, r)

total_price(e, r) = present_price(e, r) + historical_price(e, r)
```

All operations are checked `uint64_t` arithmetic. Missing prior history is
zero. Present price is recomputed from current P4R-03 overuse every epoch;
historical price persists. The canonical snapshot retains exactly resources
whose present or historical price is nonzero, sorted by
`routing::EdgeResourceKey`. Historical-only resources remain priced state but
are not current hot resources.

This schedule and its `APGAR-P4R06-*` identities are candidate-allocator
authority. They neither import nor reinterpret ADR-070's CS-RR-v1 costs.

### Hot resources and targets

- A hot resource is exactly a current P4R-02B accounting entry with positive
  overuse. Hot resources sort by descending updated total price, descending
  overuse, then canonical `EdgeResourceKey`.
- Every empty candidate pool becomes a target with structured `kEmptyPool`
  reason, no fabricated incumbent, and no fabricated hot-resource link.
- Every selected candidate is independently expanded to canonical atomic
  resources for target association. A selected net becomes a `kHotResource`
  target iff its immutable candidate touches at least one current hot resource.
  Trigger resources follow global hot-resource priority, and target impact is
  the checked sum of their updated total prices.
- Targets sort with empty-pool targets first in canonical net order, followed
  by congestion targets in descending trigger-price, descending trigger-count,
  and canonical net order.
- The plan says `kNoRegenerationRequired` only when there is no empty pool and
  no selected candidate touches a hot resource. Otherwise it says
  `kRegenerationRequired`. It does not claim feasibility, convergence, or a
  completed allocation epoch.

### Replay, bounds, and failure precedence

- Price-policy, price-snapshot, target, and complete-plan identities use stable
  value hashes. The plan identity binds configuration, prior snapshot, every
  canonical pool/candidate identity, P4R-03 outcomes/accounting, updated prices,
  hot resources, targets, and aggregate totals. These are deterministic
  integrity bindings, not hostile-operator attestations.
- Explicit hard and configured limits cover P4R-03 pools/candidates/accounting,
  epoch index, price entries, hot resources, targets, per-net and aggregate
  selected resource expansion, per-net and aggregate target-resource links,
  individual and aggregate prices, and per-net and aggregate target impact.
  Equality is accepted; the first value above a limit fails.
- Canonical price entries and canonical-net target construction check aggregate
  ceilings after each contribution, before later work can mask the first
  crossing. The target-count ceiling is checked when a canonical net first
  proves to be a target; successful targets are then sorted into the published
  priority order above.
- Precedence is configuration, Board/capacity and prior-snapshot validation,
  P4R-03 selection/accounting, epoch/price update, then target construction.
  Any fatal association, input, bound, arithmetic, resource, selection, or
  invariant error returns only a typed error. A success disposition never
  masks a fatal diagnostic, and no partial plan is exposed.

## Independent reference

Exact-small tests select directly from the submitted immutable pools, expand
candidate spans into atomic resources without production helpers, recompute
usage and overuse, then independently derive prices, hot-resource order, target
membership, target order, and aggregate totals. They do not call production
P4R-03 selection, P4R-02B accounting, or P4R-06 planning.

## Consequences

- P4R-07 receives one immutable, replayable description of what to regenerate
  and the price field under which to do it, without P4R-06 performing work on
  its behalf.
- Empty pools are actionable without pretending that they touch a congested
  resource. Historical prices survive a temporarily cold epoch without
  keeping that resource in the current hotset.
- P4R-06 remains a small allocator-domain value transformation and introduces
  no store, session, runner, schema, or publication authority.

## Explicit non-goals

This decision does not execute regeneration; call CPU or GPU route generation,
candidate building, or exact admission; mutate or publish through
`CandidateStore`; mutate candidate pools; or refresh selection after new
columns. It does not add P4R-07 execution, P4R-08 session composition,
Multi-World, GPU allocation, reusable sessions, serialized evidence,
telemetry/publication, readiness, campaign, or heldout machinery.
