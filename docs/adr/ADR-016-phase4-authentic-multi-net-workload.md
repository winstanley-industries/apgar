# ADR-016: Authentic Multi-Net Routing Contexts

**Status:** Accepted for the second Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** Per-net routing profiles, compiled contexts, route requests,
workload replay identity, and one-world roster binding

## Context

Board IR v1 was introduced for the first vertical slice with one default
routing profile. Phase 4 requires distinct nets and terminals in one Board
snapshot. Rewriting the default profile or candidate association labels would
make same-net alternatives look like a multi-net workload and would ignore
the fact that owned obstacles affect exact compilation per routed net.

## Decision

- Adopt `schemas/allocator/multi_net_workload_v1.md`.
- Adopt `schemas/allocator/one_world_v2.md` for workload-bound selection while
  retaining workload-free One-World v1 replay compatibility.
- Preserve Board IR v1's default profile for source compatibility. Add an
  explicit preparation boundary that normalizes and validates any additional
  two-terminal net profile against the same immutable snapshot.
- Compile and retain one exact context per workload net. `CompiledBoard` owns
  its prepared routing profile, and exact movement, CPU reconstruction,
  candidate admission, routing-profile fingerprinting, and GPU query metadata
  consume that retained profile instead of silently consulting the Board's
  default profile. The explicit-profile exact movement operation remains
  source-private so supported public callers cannot substitute an unprepared
  net identity to change owned-obstacle semantics.
- Bind sealed CPU and GPU producer evidence to the exact net, request
  endpoints/layers, and routing-profile fingerprint. Exact-valid geometry for
  coincident terminals on another authentic net cannot be relabeled as that
  producer's work.
- Canonicalize contexts by exact net reference, reject duplicates before
  compilation, require common Board/compiler association, and checksum the
  canonical roster and endpoints.
- Bound cumulative compiled nodes and retained compiled host bytes in addition
  to net count, and return allocation-free structured exhaustion diagnostics.
- When a one-world request binds a workload, require exactly one pool for every
  workload net, including explicit empty pools, and validate every nonempty
  pool against its net's prepared profile, rule bucket, and exact ordered
  endpoint coordinates/layers. Candidate associations deliberately allow
  multiple generation policies, so canonical immutable geometry supplies the
  retained exact-request binding after producer evidence is stripped. Record
  the workload checksum in world replay identity.
- Bind CandidateStore globally to the common Board/compiler association and
  independently bind routing-profile/rule-bucket association per net pool.
  The per-net binding persists independently of retention or transaction
  rollback, and publication work remains local to touched authentic net pools.

## Consequences

- Distinct-net candidates now retain their real terminals, rules, Board
  snapshot, compiled view, and request through allocation.
- Existing single-profile callers remain source-compatible.
- Per-net compilation is intentionally explicit in the CPU reference. Later
  corpus/performance work may deduplicate compatible prepared data only if
  exact net-owned-obstacle semantics and replay identity remain unchanged.
- This slice does not yet claim representative scale or allocator improvement.
  Negotiated prices, targeted regeneration, multi-world retention, corpus
  tiers, and equal-budget evidence remain required.
