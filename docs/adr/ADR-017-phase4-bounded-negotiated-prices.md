# ADR-017: Phase 4 Bounded Negotiated Prices

**Status:** Accepted for the third Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Present/history price updates and immutable replay state

## Context

ADR-015 established immutable capacity and price snapshots plus a deterministic
one-world CPU reference. ADR-016 established an authentic multi-net workload.
The next required allocator primitive is a bounded, independently testable
price update. Hiding present and history in one mutable scalar would make
oscillation control, replay, and later multi-world schedules ambiguous.

## Decision

- Adopt `schemas/allocator/negotiated_price_state_v1.md`.
- Bind every state to the exact Board/compiler association, Multi-Net Workload
  checksum, immutable capacity-model checksum, predecessor-state checksum, and
  source-world checksum. Saturating updates therefore retain their complete
  replay chain.
- Keep present and historical components separate. Present price is the current
  overuse times a fixed positive step. Historical price accumulates overuse
  times a separate fixed positive step. Each component and their sum are
  clamped to one configured per-resource maximum with explicit clamp bits and
  a clamp count.
- Use widened arithmetic for multiplication, accumulation, and aggregate
  totals. Reject aggregate overflow, sparse-record overflow, and iteration
  exhaustion rather than wrapping.
- Bound selections and resource vectors before hashing. Revalidate the complete
  world resource union against capacity overrides, default capacity, prior
  explicit prices, per-resource overuse, and aggregate overuse.
- Require the complete original One-World request and rerun the CPU reference
  over every original candidate pool and alternative. Pool membership,
  minimum-score selection, candidate/workload associations, selection
  evidence, scores, physical occupancy, work counters, and aggregate totals
  must reconstruct exactly; a self-checksummed public world is not trusted
  input by itself.
- Enforce the shared one-million-record cap across capacity overrides and
  generated price records so every successful state can produce its next
  immutable Price Snapshot.
- Preserve nested `ResourceExhausted` failures from snapshot construction and
  one-world reconstruction rather than diagnosing them as corrupt input.
- Produce a new immutable Price Snapshot for each state. Candidate generators
  and selectors continue to consume snapshots by const reference and never
  mutate occupancy.
- Preserve a domain-separated checksum with a representation-level golden and
  field-sensitivity tests.

## Consequences

- Identical workload, capacities, state, configuration, and world produce the
  same next state and snapshot.
- Numerical saturation is bounded and externally visible instead of silent.
- The state is a CPU reference for later multi-world and GPU differential
  implementations.
- Targeted regeneration, pin ownership, multi-world Pareto retention, corpus
  construction, and equal-budget evidence remain required before Phase 4 can
  exit.
