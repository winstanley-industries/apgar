# ADR-014: Persistent Bounded CUDA Sweep Workspaces and Compact Readback

**Status:** Proposed pending canonical benchmark evidence
**Date:** July 20, 2026
**Applies to:** Phase 3 batched planar candidate generation

## Context

ADR-013 retained CPU A* as production dispatch after the canonical Phase 3
bakeoff. That evidence identified two dominant, avoidable CUDA sweep costs:
every batch allocated and released its complete device workspace, and every
query returned four full state arrays even though host admission ultimately
needed only one reconstructed path.

The follow-up must preserve the existing trust boundary. Device output remains
untrusted, CPU A* remains the correctness oracle and disconnected-result
confirmation path, exact Board IR validation remains authoritative, and all
workspaces remain explicitly bounded by the request.

## Decision

- Retain one reusable CUDA sweep workspace on each prepared compiled view.
  One prepared-view execution lease spans execution through readback for both
  candidate batches and legacy routes. Calls on different prepared views may
  proceed independently.
- Report logical batch bytes separately from actual retained workspace
  capacity. The persistent-plus-capacity peak must fit the current request's
  device-memory bound. If a previously retained workspace is too large for a
  tighter request, discard it before allocating the smaller workspace.
- Keep the frontier generator's full-workspace protocol unchanged.
- For sweep, reconstruct one reverse state path per reached query on the
  device, then deterministically compact those query-major paths into one
  packed batch buffer.
- Version the compact path header independently. It repeats query and workspace
  ownership and supplies a checked packed-state slice.
- Transfer only result headers, compact-path headers, and the used packed state
  prefix for sweep. Continue to count bounded status synchronization
  separately.
- Treat compact paths as hostile input. The host rechecks association, bounds,
  endpoints, cycles, headings, compiled adjacency and legal-edge masks,
  resource bans and penalties, scalar cost, exact coordinates, and exact Board
  IR route legality before producing validated route evidence.
- Preserve CPU A* as production dispatch, oracle, fallback, and disconnected
  confirmation. This implementation does not establish a universal GPU
  crossover and does not change automatic dispatch.

## Measured basis

The canonical follow-up report is
`benchmarks/phase3_persistent_compact_sweep_report.md`. Its measured basis is
pending a clean final-implementation run across all eleven cases and candidate
counts `4,8,16,32,64,128,256,512`. ADR-013 remains the accepted dispatch
evidence until that checked-in raw artifact, manifest, and validation path are
complete. No exploratory median lead is treated as a win without uncertainty
qualification.

## Consequences

- Prepared-view lifetime now materially affects sweep throughput. End-to-end
  creation and release still pays upload and workspace allocation, while
  repeated compatible batches reuse capacity.
- Small jobs remain CPU-favored because GPU launch and status boundaries have
  a fixed cost. Disconnected jobs also retain the required CPU oracle cost.
- Device compaction reduces transfer volume without weakening exact host
  validation or granting the GPU authoritative route evidence.
- A future dispatch experiment should classify compatible workload size and
  prepared-view reuse explicitly. It must retain CPU fallback and publish a
  new canonical evidence artifact before changing production defaults.
- The next measured CUDA hypothesis is fewer host boundaries, such as a
  bounded persistent sweep kernel or CUDA Graph schedule. That is a separate
  decision because cancellation and partial-failure semantics must remain
  observable.
- This ADR does not expand M1 into vias, allocation, worlds, specialty routing,
  or CAD commit transactions and does not claim M1 completion.
