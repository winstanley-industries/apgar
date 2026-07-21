# ADR-018: Phase 4 Store-Backed Targeted Regeneration Planning

**Status:** Accepted for the fourth Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Deterministic congestion hotsets, complete-pool insufficiency,
and bounded column requests

## Context

ADR-017 established a trusted, replayable negotiated-price update. Column
generation must identify candidate pools that remain insufficient under the
new prices without routing every net again, trusting mutable public occupancy,
allowing scratch to escape configured bounds, or emitting a plan whose selected
candidates can be pruned before execution.

The first implementation inspected only stale source winners, undercounted
historical price exposure, retained action scratch for every eligible net, and
deferred CandidateStore pinning. An adversarial review rejected those
boundaries because an existing alternative could already avoid the conflict
and because caller-chosen pin owners did not represent independent lifetimes.

## Decision

- Adopt `schemas/allocator/targeted_regeneration_plan_v1.md`.
- Factor the production One-World validator/scorer into a source-private
  selection-only pass shared with full allocation. It canonicalizes and
  validates every pool alternative, selects exact winners, computes request and
  pool manifests, and exposes compressed selected-footprint volume without
  atomically materializing occupancy.
- Use authoritative source selection evidence to reject an oversized planning
  footprint before the negotiated-price update reconstructs atomic occupancy.
  Then compose with the complete production price update for full workload,
  capacity, selection, occupancy, counter, and checksum validation.
- Rerun the shared selector against the immutable next price snapshot. A pool
  is insufficient only when its minimum-score next-price winner still touches
  positive source-world overuse. Record both the retained source winner and the
  next-price winner.
- Include every next-winner edge in full negotiated-price exposure, including
  history-priced edges with zero current overuse. Restrict conflict metrics and
  actions to positive current overuse.
- Bound retained scratch as well as final output: keep only the possible target
  prefix in the first pass, then materialize bounded top actions only for that
  prefix. Charge source selection, next-price selection, and every retained
  target action rescan to the expanded-resource-visit budget.
- Bind the canonical complete-pool manifest and full source-request manifest in
  plan replay identity so non-winning alternatives and nonbinding limits cannot
  change silently.
- Atomically acquire one CandidateStore-issued group lease over every source
  winner and every non-null next-price winner that influenced pool sufficiency.
  Validate the complete immutable candidate value under the store lock, not
  only collision-prone identity summaries. The plan owns that move-only RAII
  lease. Independent store-issued leases reference-count overlapping
  candidates; no caller-supplied owner identity participates in correctness or
  deterministic replay.

## Consequences

- Identical authentic requests and configuration produce the same manifests,
  hotset, resource actions, budgets, metrics, and checksum independent of pool
  or candidate order.
- A conflicted stale winner does not consume regeneration budget when a retained
  alternative already avoids all current overuse under the next price state.
- A successful plan is executable while its CandidateStore remains alive;
  detached, pruned, or semantically mismatched selections fail atomically
  without partial pins. A lease that outlives the store is harmless and reports
  inactive, but no longer promises executability.
- Feasible or pool-sufficient iterations produce an explicit empty target list
  while still leasing retained source selections.
- Planning remains a bounded CPU reference. Later GPU or compressed-index
  versions require differential agreement.
- Alternative-policy synthesis, actual generation/admission, stall diagnostics,
  multi-world retention, corpus work, and equal-budget evidence remain open
  Phase 4 work.
