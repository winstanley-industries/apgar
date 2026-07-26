# ADR-060: Phase 4 Primary-Conflict Coverage

**Status:** Accepted and active for production
**Date:** July 26, 2026
**Applies to:** The next targeted-regeneration planner, executor, and reusable
CPU candidate-allocation session authorities

## Context

ADR-059 preserved two authentic negative H=4096 development observations and
kept their thin pools and rejection counts diagnostic rather than causal. A
subsequent source-and-replay investigation found a narrower planner-policy
failure that can be stated without treating pool cardinality as proof.

In calibration case 10200, 64 equally ranked conflicted nets form 32 independent
two-net primary-resource groups. The Plan-v2 target comparator resolves every
metric tie by stable net identity. With two columns per target and a 64-column
epoch cap, both observed epochs therefore spend their complete budget on the
same 32-net prefix, covering both nets from 16 groups and no net from the other
16 groups. Plan v2 behaves according to its frozen contract; changing that
retention policy is not a conformance correction.

Ranking smaller pools first was considered and rejected. Every pool starts
with one candidate, so it cannot change the first-epoch tie. A net whose
attempts repeatedly duplicate or disconnect can also remain permanently thin
and monopolize later epochs. Pool size counts identities, not current conflict
coverage, and ADR-059 explicitly does not authorize it as a causal signal.

## Decision

- Preserve Targeted Regeneration Plan v2 wire fields, checksum behavior, and
  replay bytes exactly, and adopt Plan v3 as its policy successor. Plan v3
  keeps the v2 action and target severity orders but first retains each
  eligible target's highest-ranked current conflict action.
- Define a bounded coverage capacity from the target, action, two-column, and
  source-candidate-headroom limits. For each distinct primary conflict
  resource, retain its best severity-ranked representative, then retain the
  best bounded prefix of those representatives.
- Reserve one complete-price column and one primary-resource hard-ban column
  for every coverage representative before any secondary action or
  duplicate-primary target consumes remaining opportunity. Coverage
  representatives form a canonical prefix and are authenticated by one
  plan-level `coverage_seed_target_count`.
- Build the exact-net-deduplicated union of coverage seeds and the bounded v2
  fallback, then run one ordinary target-severity-ordered remainder pass. A
  seed encountered in that pass extends its reserved record before any
  lower-ranked target; an unseeded target encountered first competes first.
  The pass charges only a seed's actions and columns beyond its reserved one
  action and two columns. A seeded target is never emitted twice.
- Require the retained-target resource rescan to reproduce the provisional
  primary action field-for-field before policy synthesis. Matching only the
  aggregate conflict count, impact, and price exposure is insufficient.
- Bound live coverage grouping by the coverage capacity and live fallback
  retention by the existing possible-target bound. The result must be
  independent of pool order. Unordered iteration and an unbounded
  resource-to-target roster are not conforming implementations.
- Preserve Targeted Regeneration Execution v5 and adopt Execution v6. V6
  accepts only Plan v3, validates the coverage prefix before query one, and
  uses new batch, success, and failed-observation checksum domains.
- Preserve CPU Candidate-Allocation Session v4 and adopt Session v5. V5
  composes Plan v3 and Execution v6 under a new session checksum domain while
  preserving the v4 ownership, preflight, epoch, fixed-point, and terminal
  Multi-World boundaries.
- Production factories accept only Plan v3, Execution v6, and Session v5.
  Plan v1/v2, Execution v1-v5, and Session v1-v4 remain historical and are
  rejected before routing, publication, mutation, or input consumption.
- Preserve every historical checksum helper and golden. Historical factories
  remain rejected before routing, mutation, or input consumption.
- Do not modify canonical algorithm-budget roster v3, Confirmatory Decision
  Protocol v2, or any existing exact, Raw, report, operational, or publication
  artifact. Their Corpus-v2/H=4096 budget preimages remain bound to Session v4,
  so all corresponding execution, controller, and hidden-worker entry points
  fail closed before fixture or preparer access. A future acquisition requires
  separately reviewed Session-v5 budget and consuming authorities. A separate
  nondecision Representative-Corpus-v1 diagnostic roster v2 may cover live
  Session-v5 tests but cannot authorize Corpus-v2 execution or publication.
- Activation includes discriminating tests for distinct-resource coverage,
  duplicate-resource representative choice, bounded eviction and re-entry,
  input permutations, cap/headroom edges, primary-action replay drift,
  cross-version rejection, unchanged historical goldens, new successor
  goldens, and field sensitivity for the seed count, seed order, primary
  action, child plan identity, and ordered column evidence. The open
  development H4096 regression must prove 32 distinct primary-resource seeds
  and 64 requested columns in epoch zero; it is not an outcome claim.

## Consequences

The successor policy guarantees bounded first-class opportunity across
distinct current primary conflicts before spending remaining work within an
already represented conflict group. It does not solve a general set-cover
problem, claim coverage of secondary actions, prioritize thin pools, or
guarantee that multiple continually failing nets on one resource rotate across
epochs. Such anti-retry semantics would require a separately versioned,
session-owned attempt ledger.

No improvement, feasibility, performance, heldout, confirmatory, Phase 4, or
M1 claim follows from this contract. Those claims remain closed until the
designated development cells are freshly reacquired under separately reviewed
Session-v5 acquisition and publication authorities from their exact clean
source commit.

## Rejected alternatives

- **Order targets by ascending source-pool cardinality.** This preserves the
  first-epoch tie and can repeatedly reward duplicate- or disconnect-only
  attempts.
- **Group a target under every conflict it touches.** A multi-resource target
  receives a guaranteed hard-ban column only for action zero; counting all
  touched resources would overstate coverage and approach an unbounded set
  cover.
- **Change Plan v2 in place.** Its stable-identity tie break is frozen replay
  behavior. Reinterpreting it would invalidate existing child and evidence
  authorities.
- **Add cross-epoch rotation now.** A deterministic attempt ledger changes
  session state, fixed-point semantics, and replay fields. It is materially
  broader than eliminating the observed cross-resource prefix starvation.
