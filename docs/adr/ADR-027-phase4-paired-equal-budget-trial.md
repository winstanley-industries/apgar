# ADR-027: Phase 4 Paired Equal-Budget Trial Boundary

**Status:** Accepted for the thirteenth Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Decision-eligible single-repetition comparison of the sequential
baseline and reusable CPU candidate allocator

## Context

Phase 4 had versioned representative cases, a traditional sequential baseline,
initial candidate preparation, a reusable allocation session, exact consumed
CPU route work, and common root-seed ancestry. It still lacked one contract
that rejected unequal opportunity before execution, normalized the two
different stopping structures, selected the correct candidate decision world,
separated deterministic semantics from operational measurement, and prevented
self-reported timing or memory from becoming decision evidence.

Running both algorithms in one process would also retain whichever contender's
allocations, caches, or worker lifecycle happened to execute first. A single
monolithic result could therefore make AB/BA ordering cosmetic and let one
contender's failed or partial state contaminate the other.

## Decision

- Adopt `schemas/benchmark/phase4_paired_trial_v1.md` as the successful-arm and
  successful-pair contract.
- Build the representative case independently inside each arm. Distill the
  result to compact semantics and destroy case, pool, route, price, trace, and
  world state before returning.
- Define equal route opportunity as `N*S == N*K + E*C`, with shared per-query
  A-star limits and exact contender aggregate caps. Normalize stopping lineage
  with `S == E + R - 1`. Equal accepted opportunity, not equal consumed work,
  is the admission rule; publish both.
- Require one root seed across sequential routing, initial preparation, and
  targeted regeneration, plus identical price, intrinsic objective, and
  One-World limits.
- Use the candidate session's preferred retained Multi-World outcome when
  present and its common-lineage One-World otherwise. Compare successful arms
  by selected nets, overuse, then unweighted intrinsic cost.
- Keep semantic checksums independent of execution order, worker count,
  timings, and measured resource use. Bind those fields through separate arm
  and pair artifact checksums.
- Capture persistent-preparer lifecycle telemetry inside the measured arm,
  require the external authority to repeat it exactly, and bind the captured
  counters into the arm artifact checksum.
- Require an externally associated isolated-process observation with enforced
  wall and peak-host-memory authority before an arm can be paired. The
  checksum-bound authority names controller-run and distinct process-instance
  identities, exact enforced limits, exit status, and before/after preparer
  telemetry. Assembly revalidates the complete record. Candidate prepared
  timing uses a persistent preparer proven reused across repetitions.
- Preserve every typed child failure in a move-only paired failure. Preparation
  and session failures retain the independently built case and caller-owned
  prepared/store state, including any authoritative post-publication store,
  until explicit reconciliation.
- Reject a declared per-epoch column opportunity that exceeds target, action,
  per-net, One-World candidate-headroom, or total-column structure. Canonically
  hash terminal schedules by key because caller schedule order is nonsemantic.
- Reject corpus query-shape pool sizes `1` and `1024` in this v1 runner rather
  than silently clamping them to Candidate-Pool Preparation v2's supported
  `4/8/16` decision sizes.

## Consequences

The next slice can implement the operating-system process controller and raw
artifact serializer without redefining algorithm fairness. AB/BA order and
worker count remain observable operational inputs, while deterministic routing
semantics can be compared independently of them. A timeout, memory kill, case
build failure, or algorithm error cannot be mislabeled a board-level loss; the
outer artifact must retain it as an incomplete attempt.

This slice does not yet define subprocess wire format, watchdog mechanism,
hardware manifest, repetition matrix, uncertainty calculation, family gate,
or a Phase 4 success decision. It does not add GPU allocation or change the
Phase 5 exact combined-route legalization boundary.
