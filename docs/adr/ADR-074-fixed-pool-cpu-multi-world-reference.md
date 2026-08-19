# ADR-074: Fixed-Pool CPU Multi-World Reference

**Status:** Accepted for P4R-09

**Date:** August 18, 2026

**Applies to:** EPIC-001 P4R-09 deterministic fixed-pool CPU Multi-World execution

## Context

P4R-08 produces one failure-atomic deterministic CPU allocation contender by
composing the P4R-06 negotiated-price planner and P4R-07 targeted-regeneration
epoch executor. Its successful and typed bounded terminal results own one
complete immutable final candidate-pool roster and one P4R-03 zero-price final
selection/accounting result. P4R-09 must test multiple allocator schedules over
that accepted contender without allowing one world to change the candidates
visible to another.

The archived ADR-020 and archived Multi-World implementation are donor and
audit material only. Their serialized schema, workload wrapper, CandidateStore
lease protocol, operational profile, and broader session design are not reboot
authority. The current resource vocabulary, P4R-03 selection/accounting,
P4R-06 price equations, and P4R-08 owning pool result remain authoritative.

## Decision

P4R-09 defines `ExecuteFixedPoolCpuMultiWorld` as one synchronous,
failure-atomic CPU reference extension over a replay-validated P4R-08 terminal
result. It performs no route generation, candidate admission, store mutation,
publication, pruning, or board mutation.

### Eligible source and common branch state

- All three valid P4R-08 terminal reasons are eligible: `kFixedPoint`,
  `kEpochBoundExhausted`, and `kSessionBoundExhausted`. A fatal P4R-08 error is
  not a source value and cannot be made eligible.
- The source is accepted only when its supplied P4R-08 configuration, session
  identity, final-pool identity, step/snapshot lineage, counters, terminal-plan
  shape, final selection, Board IR association, capacity association, and
  immutable candidate roster replay successfully. A mismatched or malformed
  lineage fails before any world executes.
- The common source is exactly the P4R-08 complete owning final-pool roster.
  Candidate order is canonical Candidate ID order inside canonical net order.
  No selected-only projection or rebuilt candidate may replace it.
- The common starting allocator state is explicitly the replay-validated
  P4R-08 final P4R-03 zero-price selection/accounting, an empty negotiated-price
  map, and zero completed price updates. Its stable identity binds the P4R-08
  session identity, final-pool identity, resource lattice, capacity, and complete
  common selection/accounting. A pending P4R-08 terminal plan snapshot is not
  adopted as branch state.
- Every canonical schedule branches independently from that same state. A
  world receives value copies of price and trace state and cannot observe or
  mutate another world's state.

### Canonical schedules, selection, and price updates

- A schedule has a nonzero stable key, one P4R-06
  `NegotiatedPriceUpdatePolicyV1`, and a positive maximum selection-round
  count. Schedules canonicalize by key. Duplicate keys and duplicate exact
  policy/round schedules fail closed.
- Each round selects one candidate or the existing P4R-03 `kEmptyPool` absence
  for every canonical pool. For candidate `c` under the world's current price
  map, the checked negotiated score is

  ```text
  score(c) = intrinsic_base_cost(c)
           + sum(total_price(r) * usage(c, r))
  ```

  Candidate footprints use the current canonical atomic
  `routing::EdgeResourceKey` vocabulary. The ordering is negotiated score,
  followed by the unchanged P4R-03 total order: intrinsic base cost, via count,
  bend count, mathematical total step count, axis-aligned DBU length, diagonal
  DBU projection, and Candidate ID. Therefore an empty price map reproduces
  P4R-03 ranking exactly; P4R-03 itself is not changed.
- Selected-candidate accounting uses the current P4R-02B binary-capacity and
  atomic-usage semantics. The executor does not reinterpret candidate costs,
  spans, capacity, or overuse.
- If another round is permitted, price update `e`, starting at zero, uses the
  P4R-06 equations independently in that world:

  ```text
  present_factor(e) = initial_present_factor
                    + e * present_factor_increment

  present_price(e, r) = present_factor(e) * overuse(e, r)

  historical_price(e, r) = historical_price(e - 1, r)
                         + historical_price_increment * overuse(e, r)

  total_price(e, r) = present_price(e, r) + historical_price(e, r)
  ```

  Missing prior history is zero. Historical-only entries remain. Every
  multiplication, addition, factor, score, objective, count, and aggregate is
  checked `uint64_t` arithmetic.

### Terminal policy and retained outcomes

- Deterministic round-boundary precedence is feasible, no candidate without
  regeneration, then selection-round bound. Feasible means every pool selected
  one candidate and total overuse is zero. No-candidate terminates immediately
  because fixed-pool P4R-09 cannot generate a missing column.
- The common comparable terminal objective is selected-net count maximized,
  total overuse minimized, and unweighted total intrinsic base cost minimized.
  Schedule-authored price policy affects search but not the retained quality
  objective.
- A terminal world is Pareto eligible exactly when its missing-net count and
  total overuse do not exceed the configured near-feasible thresholds. Every
  feasible world is consequently eligible under valid nonnegative thresholds.
- Strict Pareto dominance requires no worse values in all three objective
  dimensions and a strict improvement in at least one. Every nondominated
  eligible world is retained. Equal objective points do not dominate one
  another and remain independently retained.
- If the exact frontier exceeds any configured world, selection-record,
  resource-record, or price-record retention ceiling, execution fails with a
  typed retention-bound error. The frontier is never approximated or silently
  truncated.
- One preferred retained outcome is selected by this explicit total order:
  fewer missing nets, lower total overuse, lower total intrinsic cost,
  terminal-reason order `feasible`, `no candidate`, `round bound`, then lower
  canonical schedule key and world identity. No preferred result exists when
  no world is eligible.

### Whole-execution bounds, precedence, and atomicity

- Positive hard and configured ceilings cover worlds, total scheduled rounds,
  price updates, source candidates, candidate evaluations, candidate atomic
  resource visits, selected-resource accumulation, net outcomes, emitted price
  entries, trace records, buffered terminal selection/resource/price records,
  pairwise Pareto comparisons, and retained terminal records.
- Before branch execution, widened checked products reserve the entire declared
  schedule roster against the immutable pool shape. Equality is accepted; the
  first value above a ceiling fails. Runtime counters charge actual early-stop
  work and must not exceed the reservation.
- Precedence is configuration, Board/capacity and P4R-08 replay validation,
  complete source-roster validation, canonical schedule validation,
  whole-execution reservation, canonical branch execution, exact Pareto
  retention, preferred selection, and final identity assembly.
- Invalid input, invalid schedule, association or lineage mismatch, bound or
  retention exhaustion, arithmetic overflow, selection/accounting failure,
  host resource exhaustion, and internal invariant failure return only a typed
  error. No summaries, completed branches, retained worlds, counters, or
  preferred result escape a fatal execution.
- The Board IR, capacity model, P4R-08 result and configuration, schedules, and
  execution configuration are never mutated.

### Replay identities

- Stable value identities bind the canonical final-pool source, common starting
  state, every schedule, per-world branch identity, every input/output price
  state, complete round trace, terminal selection/accounting and objective,
  terminal reason, eligibility and retention decisions, counters, preferred
  result, and complete execution.
- Schedule caller order, source input order used to produce an equivalent
  P4R-08 result, pointer values, allocation addresses, and repeated execution
  are not semantic.
- These in-process hashes are deterministic replay bindings, not serialized
  evidence and not hostile-operator attestations.

## Independent exact-small reference

Exact-small tests implement a separate fixed-pool Multi-World state machine.
It does not call `ExecuteFixedPoolCpuMultiWorld`, its selection comparator,
price-update routine, Pareto helpers, preferred-order helper, counter projector,
or identity helpers. It directly expands admitted candidate spans, reproduces
selection and P4R-02B accounting, advances the P4R-06 equations, derives every
terminal objective and decision, performs pairwise dominance and preferred
ordering, accumulates counters, and independently hashes source, state, trace,
outcome, retention, and execution identities.

Coverage includes common-state isolation; one and multiple worlds; P4R-08
fixed-point, epoch-bound, and session-bound sources; canonical schedule and
source-input permutations; repeated-run determinism; feasible, no-candidate,
and round-bound termination; exact-bound equality and one-under failures;
checked arithmetic overflow; strict dominance and equal-point retention;
eligibility thresholds; exact-frontier retention exhaustion; every preferred
tie-break; invalid schedules, lineage, and associations; identity sensitivity;
fatal-error atomicity; and input immutability.

## Consequences

- P4R-09 provides the deterministic CPU reference needed to ask whether
  independent allocator schedules improve a fixed accepted P4R-08 pool without
  confounding that question with world-dependent column generation.
- Retained worlds contain complete selection/accounting, price, trace, and
  termination values but do not acquire store leases or create a new candidate
  ownership authority. Candidate IDs remain bound to the owning P4R-08 source.
- Interleaved world-dependent regeneration or publication requires a later
  separately authorized barrier contract and is not implied here.

## Explicit non-goals

This decision does not begin P4R-10; add GPU allocation; regenerate, admit,
prune, publish, or mutate candidates; interleave world-dependent work; reuse
CS-RR-v1 costs; change P4R-03 ranking; legalize or commit board geometry; port
archived schemas or CandidateStore leases; or add serialized evidence, runners,
telemetry, readiness, campaign, operational-capture, publication, or heldout
machinery.
