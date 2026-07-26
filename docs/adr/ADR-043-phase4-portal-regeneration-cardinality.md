# ADR-043: Phase 4 Portal-Regeneration Column Cardinality

**Status:** Accepted for Phase 4 remediation; allocator child authorities
superseded for production by ADR-060
**Date:** July 23, 2026
**Applies to:** Targeted-regeneration planning, execution, composed CPU
allocation, and the Phase 4 V2 evidence protocol

## Context

The authentic Phase 4 V1 matrix showed that the candidate allocator did not
improve portal-channel heldouts. Investigation found a versioned planner
defect: Plan v1 bounded requested columns by `conflict_resource_count`, while
policy synthesis reserves column zero for a price-only search and uses later
columns for ordered hard bans. A target with one conflict could therefore
request only the price-only column and could never test its sole hard-ban
alternative.

Changing that rule under Plan v1 would rewrite checksum-bound replay semantics.
Execution v4 and Session v3 also freeze their child authorities. The preserved
V1 matrix and its negative decision must remain reproducible.

## Decision

- Preserve Targeted Regeneration Plan v1 and adopt Plan v2. V2 computes the
  per-target cardinality as `min(max_per_net, conflicts + 1, actions + 1)` in
  widened arithmetic before applying total-column and candidate-headroom caps.
- Preserve Execution v4 and adopt Execution v5. V5 explicitly requires Plan
  v2 and uses new batch, success, and failure checksum domains.
- Preserve Session v3 and adopt Session v4, which composes Plan v2 and
  Execution v5 under a new session checksum domain.
- Keep the Phase 4 paired wire at v1: it already checksum-binds the complete
  nested session configuration and allocator outputs. V2 observations require
  a new frozen budget roster and protocol authority; they do not supersede or
  reinterpret V1 evidence.
- Separate the validation-only frozen-V1 budget-preimage builder from the
  ordinary executable V1 canonical builder. The former reconstructs Session-v3
  manifest checksums and is intentionally rejected by current execution; the
  latter named the then-current Session v4 so existing V1 diagnostics remained
  runnable. ADR-060 now supplies Session v5 and the nondecision diagnostic
  roster v2 successor.
- Freeze
  `schemas/benchmark/phase4_current_v1_diagnostic_budget_roster_v1.md` as a
  Session-v4, `decision_eligible=false` authority for only the five
  cross-process test cells. Production Raw-v1/Wire-v2 validators remain bound
  exclusively to the Session-v3 representative manifest. The evidence runner
  rejects publishable output until the new V2 manifest and protocol are frozen;
  its current V1 execution path is available only through the explicit
  unstamped testing seam.
- For V2 equal-budget trials, permit two columns per targeted net while
  retaining exactly `N` total columns per epoch. Candidate opportunity remains
  `N*K + E*N`, matching a sequential baseline with `K + E` sweeps. Total route
  and work opportunity is not increased.
- A forced-ban-capable V2 budget must reserve one additional policy entry in
  the maximum per-candidate draft bound. Aggregate generated, admission-input,
  and related byte caps must derive from that corrected bound.
- Before any heldout observation, use only the V2 calibration firewall to
  verify portal behavior and freeze the new manifest, budget roster, and
  protocol authorities.
- The permitted case-10200, pool-8 calibration selects equal-arm negotiated
  price steps `(present = 1, history = 2250)`. With `64` selected nets and
  exactly `128` regeneration queries, the reusable contender reduces board
  overuse from `32` to `21` with zero exact-admission rejection; a repeated
  execution is byte-for-byte semantically and diagnostically identical. The
  retained epoch record also proves that at least one executed route query had
  `column_index > 0`, directly witnessing an ordered hard-ban column. This is
  calibration evidence only and does not authorize a heldout claim.

## Consequences

Singleton conflicts can now generate both the complete-price route and the
sole ordered hard-ban alternative without increasing total equal-budget
opportunity. Old plan, execution, session, matrix, and publication artifacts
retain their original meaning. Phase 4 remains incomplete until an authentic
V2 confirmatory matrix passes every frozen exit gate.
