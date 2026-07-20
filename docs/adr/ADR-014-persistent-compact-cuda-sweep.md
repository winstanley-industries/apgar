# ADR-014: Persistent Bounded CUDA Sweep Workspaces and Compact Readback

**Status:** Accepted for GPU-heavy prepared batch exploration; CPU dispatch unchanged
**Date:** July 20, 2026
**Applies to:** Phase 3 batched planar candidate generation

## Context

ADR-013 retained CPU A* as production dispatch after the canonical Phase 3
bakeoff. That evidence identified two dominant avoidable CUDA sweep costs:
every batch allocated and released its complete device workspace, and every
query returned four full state arrays even though host admission ultimately
needed only one reconstructed path.

The follow-up preserves the trust boundary. Device output remains untrusted,
CPU A* remains the correctness oracle and disconnected-result confirmation
path, exact Board IR validation remains authoritative, and all workspaces
remain explicitly bounded by the request.

## Decision

- Retain one reusable CUDA sweep workspace on each prepared compiled view.
  One prepared-view execution lease spans execution through readback. Calls on
  different prepared views may proceed independently.
- Report logical batch bytes separately from actual retained workspace
  capacity. Persistent plus capacity is the simultaneous owned VRAM peak. A
  retained workspace that violates a tighter request is evicted before the
  smaller allocation.
- Keep the frontier generator's full-workspace protocol unchanged.
- For sweep, construct one reverse state path per reached query, scan compact
  lengths, and deterministically scatter query-major paths into one packed
  batch buffer. Transfer only result headers, compact headers, and the used
  packed prefix; bounded status synchronization remains separate.
- Treat compact paths as hostile. The host rechecks ownership, bounds,
  endpoints, cycles, headings, adjacency, legal-edge masks, bans, penalties,
  scalar cost, exact coordinates, and exact Board IR legality before sealing
  route evidence.
- Above 64 admitted queries, validate deterministic contiguous partitions with
  at most eight host workers. Each partition owns its result slots and visited
  bitset. Worker exceptions are rethrown in ascending partition order, and
  thread-creation fallback preserves partition ownership and output order.
- Convert validated routes with one bounded sorted batch membership index,
  while retaining every immutable seal, association, and authenticated-CUDA
  producer check. Its overlapping payload is included in the deterministic
  host peak.
- Measure prepared full-pipeline throughput after one identical untimed warm-up
  that fills the workspace and passes differential/admission checks. Timed work
  includes generation, compact readback, hostile validation, exact admission,
  a fresh bounded store, metrics, and transient-result release.
- Preserve CPU A* as production dispatch, oracle, fallback, and disconnected
  confirmation. Any future automatic crossover rule requires a separate
  workload-aware decision and evidence artifact.

## Measured basis

The canonical result is
`benchmarks/results/phase3_persistent_compact_sweep_c68829c.json`, SHA-256
`f766e267cfd5ab740a0bb4980cd75ae74e995e0c2128a297b7b5ab39328b0783`,
measured from exact clean commit
`c68829c62db8c01bee81c30d106adf4ff599ae43` on an RTX 5080 / compute
capability 12.0 with KMD 610.62 and checksum-pinned hermetic toolchains. The
report is `benchmarks/phase3_persistent_compact_sweep_report.md`.

The validator-bound summaries are:

```text
{"generator_stage_medians":792,"reached_queries":9331,"unexpected_failure_queries":0,"unique_requested_queries":11220,"unreachable_queries":1889}
{"end_to_end":{"clear_cpu_mean_wins":81,"clear_cuda_mean_wins":7,"comparisons":88,"equal_row_geomean_cuda_speedup":0.30946512503839624,"nominal_cpu_median_wins":81,"nominal_cuda_median_wins":7,"nominal_median_ties":0,"pooled_cuda_throughput_speedup":0.6913863827970889,"unclear_mean_differences":0},"execution_readback":{"clear_cpu_mean_wins":62,"clear_cuda_mean_wins":23,"comparisons":88,"equal_row_geomean_cuda_speedup":0.36894891000697294,"nominal_cpu_median_wins":62,"nominal_cuda_median_wins":26,"nominal_median_ties":0,"pooled_cuda_throughput_speedup":0.9505446967777404,"unclear_mean_differences":3},"prepared_end_to_end":{"clear_cpu_mean_wins":56,"clear_cuda_mean_wins":27,"comparisons":88,"equal_row_geomean_cuda_speedup":0.5103362865877366,"nominal_cpu_median_wins":61,"nominal_cuda_median_wins":27,"nominal_median_ties":0,"pooled_cuda_throughput_speedup":1.021244910038941,"unclear_mean_differences":5}}
```

All 792 non-upload median generator/stage rows are ordered, deterministic, and
CPU-oracle differential. The unique 11,220-query matrix contains 9,331
reachable and 1,889 unreachable policy outcomes, with no invalid, unsupported,
resource, cancellation, backend, validation, or invariant failures.

Prepared full-pipeline workload-pooled throughput is 1.021x CPU over the full
matrix and crosses per-count pooled throughput at `k=64`. Equal-row geometric
mean reaches 0.999x at `k=128` and exceeds CPU at `k=256` (1.017x) and `k=512`
(1.009x). CUDA nevertheless wins only 27 of 88 prepared medians. Cold
end-to-end pooled throughput is 0.691x CPU and never crosses. The accepted v2
common-matrix comparison shows 1.469x CUDA sweep execution improvement and
1.252x cold improvement while CPU controls remain within roughly 1.6%.

## Consequences

- The Phase 3 objective is met for prepared, sufficiently large, GPU-heavy
  candidate exploration, not for every board or latency scope.
- Prepared-view lifetime and workload classification are part of the
  performance contract. Small batches, disconnected workloads, and cold view
  creation remain CPU-favored.
- CPU A* remains current production dispatch. A future crossover experiment
  should include batch size, prepared-view reuse, corpus shape, and expected
  disconnected fraction while preserving exact CPU fallback.
- Compact transfer volume and reusable capacity reduce host/device overhead
  without weakening exact validation or granting the GPU authoritative route
  evidence.
- Execution-only timing is noisy; dispatch decisions use the stable prepared
  full-pipeline scope and uncertainty-qualified comparisons.
- Future CUDA work should first reduce host boundaries and repeated kernel
  launches, then profile long-run propagation. Persistent kernels or CUDA
  Graphs require a separate cancellation and partial-failure decision.
- This ADR does not expand M1 into vias, global allocation, worlds, specialty
  routing, or CAD commit transactions and does not claim M1 completion.
