# ADR-025: Reusable CPU Candidate-Allocation Session

**Status:** Accepted for the eleventh Phase 4 vertical slice
**Date:** July 20, 2026
**Applies to:** The composed reusable-candidate CPU contender used by Phase 4
equal-budget evidence

## Context

ADR-015 through ADR-020 established the resource model, authentic multi-net
workload, One-World and negotiated-price references, bounded targeted
regeneration, and fixed-pool Multi-World search. ADR-023 added persistent
production CPU pool preparation, while ADR-024 named an algorithmically
independent sequential baseline. These components did not yet define one
lifetime-safe candidate-allocation invocation that owns the shared store,
repeats regeneration deterministically, freezes a final pool, and exposes the
board-level contender outcome required by the evidence runner.

The composed owner must also close a transactional failure gap. Targeted
Regeneration Execution v2 can report a failure after its atomic CandidateStore
publication committed. A factory that consumes and destroys the sole store on
that error would erase authoritative state while claiming only an observation.

## Decision

- Adopt `schemas/allocator/cpu_candidate_allocation_session_v1.md`.
- Accept the Board, workload, capacities, and prepared pools by rvalue
  reference, but move them into the session only after every composed step
  succeeds. On error they remain caller-owned. A post-publication failed
  observation therefore retains the exact authoritative CandidateStore and
  immutable context needed for reconciliation.
- Own the preparation and sole mutable CandidateStore inside the successful
  move-only session. Expose only its const preparation view. Declare the
  terminal Multi-World result after the store owner so retained leases release
  before store destruction.
- Build one initial negotiated-price state and One-World allocation from the
  authentic prepared pools. If infeasible, repeat bounded Targeted
  Regeneration Execution v2 epochs on one common One-World lineage. Replace
  the full pools, state, request, and world only after one successful atomic
  epoch, and retain compact immutable evidence rather than lease-bearing prior
  executions.
- Require unchanged complete pool semantics, selected route semantics, and
  complete price values before declaring a fixed point. Complete pool equality
  compares the typed immutable roster; manifest checksums are replay evidence,
  and disagreement is a collision-or-drift invariant failure. A stalled
  generator, unchanged candidate ID, or unchanged selected route alone is
  insufficient.
- After regeneration stops, freeze the complete pool and run one fixed-pool
  Multi-World Execution v1 across canonical schedules from the final common
  price state. Preserve its exact Pareto archive and stable preferred world in
  the owned session result.
- Keep interleaved Multi-World column publication out of v1. It requires a
  separate global freeze/gather/one-publication/refresh barrier; sequentially
  invoking the existing single-plan executor for worlds would make schedule
  order semantic. The v1 contender instead completes single-lineage
  regeneration before terminal fixed-pool Multi-World exploration.
- Preflight the whole configured regeneration envelope and terminal
  Multi-World worst case before the initial allocation, including policy
  projections, CandidateStore transaction input/exact-work and cumulative
  rejection retention, source authentication, buffered and retained terminal
  records, retained worlds, and winner pins. Reject count-known failures before
  footprint traversal and stop footprint inspection at the first rejected
  charged span. Recheck exact cumulative epoch work before query one. Equality
  succeeds; overflow and one-under fail before CPU routing or store mutation.
  Resource-refinement sessions reserve only fixed source authentication: no
  hypothetical regeneration transaction, branch round, Pareto, or terminal
  branch-record work is charged.
- Make the session the sole authority for known unmapped exact conflicts.
  Nested conflict fields remain zero, and refinement outranks apparent
  feasibility, fixed point, or budget termination.
- Bind all semantic configuration, component identities, cumulative counters,
  compact epoch/column evidence, final pool and rejection manifests, and final
  price/One-World/Multi-World identities into one stable session checksum.
  Worker count, address, lease identity, wall time, and completion order remain
  operational only.
- Canonicalize a private configuration copy by schedule key before execution,
  storage, and hashing. Final result assembly performs only nonthrowing moves
  from caller inputs. Move assignment is intentionally deleted so replacement
  cannot destroy the owned store before releasing an existing terminal lease.

## Consequences

Phase 4 now has two executable CPU algorithms with a real architectural
distinction: the sequential baseline retains one current route and exposes
route-then-commit dependency, while the candidate-allocation session prepares
and reuses immutable alternatives, selects globally, regenerates targeted
columns atomically, and explores terminal fixed-pool worlds.

The successful result is lifetime-self-contained, and post-publication failure
cannot silently destroy committed store state. Compact epoch records prevent
prior source/successor leases from accumulating across a long session. The
terminal Multi-World lease proves selected candidates remain attached to the
owned store.

The representative differential now exercises a real regeneration epoch and
independently composes the child targeted-regeneration and terminal Multi-World
executions. Separate calibration coverage executes the full owned session at
4, 8, and 16 requested candidates per net.

This slice does not add equal-budget measurement, a GPU contender, a global
multi-world regeneration barrier, combined exact legalization, host-CAD
validation, or a Phase 4 success claim. Those require later reviewed slices
and canonical evidence.
