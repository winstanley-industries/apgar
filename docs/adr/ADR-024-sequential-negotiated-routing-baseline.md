# ADR-024: Sequential Negotiated-Routing Baseline

**Status:** Accepted for the tenth Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** The named traditional sequential baseline for Phase 4
candidate-allocation evidence

## Context

Phase 4 must compare reusable candidate allocation against a named sequential
negotiated-routing baseline under equal budgets. Existing APGAR One-World,
multi-world, and targeted-regeneration paths all consume reusable candidate
pools, so none is an independent traditional baseline. Reusing them under a
different label would make the comparison circular.

A credible baseline must retain the production CPU A* and exact candidate
admission contracts while exposing the characteristic sequential dependency:
each routed net sees the routes committed by earlier nets. It must also be
bounded, replayable, and independently checked against the existing resource
accounting and price-update references.

## Decision

- Adopt `schemas/allocator/sequential_negotiated_baseline_v1.md` as the named
  Phase 4 sequential baseline.
- Process canonical workload order in complete sweeps. Before routing one net,
  remove its prior winner from baseline-local occupancy. Freeze one immutable
  policy from the previous completed sweep's history prices plus prospective
  overuse against the routes currently committed by other nets. Run the
  bounded production CPU A*, exact-build and exact-admit at most one
  replacement, then commit it before processing the next net.
- Retain only one current immutable route per net. Routes from earlier sweeps
  are not reusable alternatives and never enter selection. If a later attempt
  is disconnected, unsupported, or rejected before admission, restore the
  prior exact route and preserve both the attempt outcome and restoration flag.
  Release the previous singleton pools and One-World result before the next
  sweep's route generation, and bound the peak current-winner byte roster so
  the implementation cannot silently retain two route generations.
- Use a fresh one-item CandidateStore for every query. Its lifetime ends after
  exact admission, while the immutable shared candidate handle remains owned by
  the session result. No store mutation is shared across route queries.
- At every sweep boundary, construct exactly one-candidate or explicit-empty
  pools and run the One-World CPU reference as an independent resource-usage
  oracle. Baseline-local occupancy must equal its selected-footprint usage.
  Update bounded negotiated prices exactly once from that authenticated world.
- Declare feasibility only when every net has a candidate and total resource
  overuse is zero. Declare a fixed point only after both route semantics and
  every negotiated-price value and clamp flag remain unchanged across a
  completed sweep. A first unchanged route roster is insufficient. Explicit
  empties and the sweep budget retain separate terminal reasons.
- Reject known exact conflicts absent from the resource vocabulary as
  `resource_refinement_required`; they cannot be reported as feasible or
  stalled.
- Preflight nets, sweeps, queries, CPU A* work, policy projection and entries,
  expanded rip-up/commit visits, occupancy records, candidate bytes,
  structured rejection evidence, trace bytes, CandidateStore input and exact
  work, and One-World limits before the first query. Conservatively prove that
  the worst bounded route/policy shape fits the draft cap before CPU A* can
  allocate that draft. Enforce the aggregate counters again at runtime.
- Build congestion penalties in canonical order and linearly merge them with
  the sorted normalized base policy. Charge every roster and merge visit to the
  aggregate policy-work bound; supported high-suffix inputs must not trigger
  quadratic sorted-vector insertion.
- Return a self-contained move-only result with ordered query and sweep traces,
  one final pool per workload net, the last independently reconstructed world,
  its successor price state, semantic identities, and a stable session
  checksum. Board/workload pointers, wall time, allocator addresses, and
  operating-system scheduling are excluded from replay semantics.
- Preserve a complete canonical Candidate Rejection v1 record for every draft
  or store rejection. Recompute its logical-byte accounting, cap both each
  record and aggregate retained rejection bytes, and bind the full record into
  the session checksum; a rejection code alone is insufficient evidence.

## Consequences

Phase 4 now has an executable baseline that is algorithmically independent of
candidate reuse: it performs one route search per net per sweep and exposes the
traditional route-then-commit dependency. The shared One-World and negotiated
price primitives are used only after a sweep as independent accounting and
state-transition oracles.

The query trace also records prospective congestion-resource count and total
added congestion penalty. Those fields make earlier-net commit and current-net
self-rip-up directly testable without treating a chosen route as the policy
oracle. Stable semantic encoding fixes the exact width and order of candidate
schema fields, and zero batch hashes are remapped to the nonzero scheduling
identity required by candidate generation.

The synchronous implementation deliberately creates no threads. Equal-budget
evidence may place invocations on a persistent production worker owned by the
future evidence runner, but concurrency cannot change baseline semantics.
Candidate allocation may reuse multiple stored alternatives; this baseline may
not. That distinction is the comparison under test.

This slice does not execute the complete candidate-allocation session, freeze
timing and memory measurement, add GPU allocation, legalize combined routes,
or claim Phase 4 success.
