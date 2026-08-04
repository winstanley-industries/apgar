# ADR-070: Canonical Sequential Negotiated-Routing Baseline

**Status:** Accepted for P4R-05

**Date:** August 4, 2026

**Applies to:** EPIC-001 P4R-05 deterministic CPU comparison baseline

## Context

Phase 4 must eventually compare candidate allocation with one named sequential
baseline under equal budgets. P4R-02B established the canonical physical-edge
capacity and accounting vocabulary, while Phase 3 already supplies production
CPU A*, authenticated candidate construction, and exact admission. P4R-03 and
P4R-04 operate on immutable candidate pools, but a sequential comparison arm
must generate and negotiate one route at a time without silently becoming a
pool consumer or an early version of P4R-06 allocator pricing.

Sequential rip-up-and-reroute also needs a complete policy. Unspecified net
order, incumbent behavior, price timing, rejection handling, or termination
precedence would make the comparison irreproducible. Unbounded passes, route
work, candidate material, resource expansion, diagnostics, or integer costs
would make a nominal baseline unsafe to execute on adversarial input.

## Decision

P4R-05 names **Canonical Sequential Rip-Up-and-Reroute v1 (CS-RR-v1)** as the
CPU sequential negotiated-routing baseline.

### Inputs and initial state

- One immutable Board IR snapshot, the existing immutable
  `ResourceCapacityModel`, and exactly one retained prepared `CompiledBoard`
  plus planar request are required for every requested net.
- Requests are canonicalized by Board IR `EntityRef`. Caller order and
  `CompiledBoard` address are not semantic. Duplicate and unknown nets fail
  before routing.
- Every prepared compiler context must authenticate the Board IR and the common
  resource lattice carried by the capacity model.
- Pass zero starts with no current routes, zero occupancy, and no historical
  congestion costs. Only baseline-local current routes, atomic usage, and
  congestion costs are mutable.

### Pass and query policy

- Passes are numbered from zero. Every pass attempts every net once in
  canonical net order.
- Before a reroute, the net's incumbent usage is removed from current
  occupancy. CPU A* then reads an immutable per-query cost snapshot; it never
  mutates occupancy, prices, Board IR, or compiler state.
- The pass-present factor is
  `initial_present_cost + pass_index * present_cost_increment`.
- For resource `r`, the query snapshot adds the checked integer cost
  `history[r] + present_factor * max(0, usage[r] + 1 - capacity[r])`, where
  `usage` excludes the removed incumbent. The snapshot uses sorted canonical
  `routing::EdgeResourceKey` entries and is composed into the existing
  request-local `ResourcePenalty` policy consumed by production CPU A*.
- Because the binary capacity model also permits zero, a nonzero present
  factor at zero capacity snapshots every legal, non-banned canonical edge in
  that net's retained compiler view; unoccupied edges are not silently free.
  The existing candidate-policy resource-entry ceiling bounds that expansion.
- After a nonterminal completed pass, each resource overused in that pass's
  production P4R-02B accounting receives the checked update
  `historical_cost_increment * overuse_units`. History never changes during a
  pass.
- Caller-authored base bans remain bans; the baseline does not attach a
  redundant congestion penalty to a banned resource. All base and snapshot
  penalties are normalized by the existing candidate-policy authority. The
  configured congestion-cost bound covers their maximum combined per-resource
  value.
- Candidate ordinal is the canonical pass index. Run identity is derived from
  normalized inputs and bounds; query identity is derived only from run, net,
  and pass identities. Scheduling order and addresses never enter either.

### Route lifecycle and incumbents

- Every reachable attempt calls production CPU A*, authenticated CPU candidate
  construction, and exact admission. No `RouteCandidate` field is fabricated
  by the baseline.
- A newly admitted route is installed only after its canonical resource spans
  have been expanded within bounds. The admitted candidate remains immutable.
- Disconnected and unsupported CPU routes, candidate-build rejection, and
  exact-admission rejection are ordinary per-net outcomes. If no incumbent
  exists, the final net state records the matching structured absence.
- An ordinary rejected reroute restores and retains its incumbent. The
  diagnostic records that retention. A successful admitted replacement is the
  only event that discards the old incumbent.
- Temporary overuse is permitted only in baseline-local current occupancy.
  Route generation still observes immutable exact Board IR and conservative
  compiler legality, and every installed route has passed exact admission.

### Pass boundaries and termination

- At every completed pass boundary, P4R-02B `ResourceAccounting` is computed
  over exactly the current admitted routes.
- A feasible result requires every requested net to retain a route and final
  total overuse to be zero.
- Stall means two consecutive completed passes have the same canonical
  geometry/resource signatures or the same structured absence for every net.
  Candidate provenance and identity changes alone do not defeat stall.
- Deterministic termination precedence is: any fatal error returns no
  board-level result; otherwise feasible precedes stalled, and stalled precedes
  pass limit at the same completed boundary.
- Stalled and pass-limited results with missing routes or positive overuse are
  valid bounded infeasible board outcomes, not infrastructure failures.

### Bounds and failure atomicity

- Hard and configured limits cover net count, pass count, total attempts,
  per-query and aggregate CPU work, per-attempt and aggregate generated
  candidate bytes, retained candidate bytes, per-candidate and aggregate
  expanded resource uses, retained diagnostics, and congestion-cost values.
- Net/pass, attempt/work, attempt/byte, attempt/resource, retained-byte, price,
  usage, and update arithmetic is checked before use. Equality with a bound is
  accepted; the first value above it fails deterministically.
- Invalid configuration or request, association mismatch, bound exhaustion,
  arithmetic overflow, host resource exhaustion, and internal invariant
  failure are distinct typed fatal results.
- All working state is local. A fatal result destroys it and exposes no
  partially updated board-level outcome.
- A successful or valid infeasible result contains one canonical immutable
  final outcome per requested net, final accounting over exactly retained
  routes, the termination reason, and pass/attempt/work/byte/resource totals.
  It contains no pointer-derived or input-order-derived fields.

## Independent reference

Exact-small tests maintain an independent sequential state machine. It may
compose the Phase 3 route/build/admit primitives, but it does not call the
production CS-RR-v1 entry point. It expands final admitted
`PhysicalEdgeSpan`s directly into atomic `EdgeResourceKey` uses instead of
calling production resource accounting.

## Consequences

- CS-RR-v1 is deliberately deterministic and bounded rather than adaptive.
  Its canonical order creates an explicit, reproducible sequential bias for
  the later equal-budget comparison.
- A legal alternative can be selected in a later pass because an earlier net
  sees other incumbents after its own deterministic rip-up.
- Ordinary reroute rejection cannot erase a previously admitted legal route.
- Baseline-local present/history costs are comparison-arm implementation state.
  They are not candidate-allocator prices and are not reusable by P4R-06.
- The slice is slightly above EPIC-001's 2,500-line extra-scrutiny threshold
  because the production state machine, independent state/accounting oracle,
  and discriminating bound/failure matrix establish one inseparable board-level
  result. Splitting those checks would leave the baseline contract temporarily
  self-confirming or unbounded rather than deliver an independent outcome.

## Explicit non-goals

This decision does not build or consume candidate pools, call the P4R-04
preparer or P4R-03 selector, change CandidateStore retention, define P4R-06
prices or targeted regeneration, add Multi-World or GPU allocation, create a
reusable allocation session, or add a serialized evidence schema, runner,
telemetry, publication, readiness, campaign, or heldout mechanism. It does not
port the archived sequential baseline.
