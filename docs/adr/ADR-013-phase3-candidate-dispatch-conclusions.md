# ADR-013: Phase 3 Candidate Dispatch Conclusions

**Status:** Accepted
**Date:** July 19, 2026
**Applies to:** Phase 3 CPU/GPU candidate-generation dispatch and the retained
experimental CUDA generators

## Context

Phase 3 tests whether many independent detailed searches sharing one immutable
prepared CompiledBoard upload make GPU candidate exploration attractive. The
comparison must include reconstruction, exact Board IR admission, immutable
store publication, and upload cost rather than promote a backend from kernel
throughput alone. CPU A* remains the correctness oracle and production
dispatch unless reproducible end-to-end evidence supports a change.

The evidence is the Google Benchmark v1 artifact
`benchmarks/results/phase3_candidate_bakeoff_eee3794.json`, SHA-256
`2e8e4beff7b1e6f281f5b170e9f7253403925a013e95a620f888f8395f0a6462`,
built from exact source commit `eee37945ba005e535c6dde549e083ffca7cd249d`.
It covers eleven versioned corpus cases, candidate-pool requests of 4 through
128, sequential and parallel CPU A*, batched CUDA heuristic frontier, and
batched CUDA sweep. Each benchmark uses twenty repetitions, a 20 ms minimum
sample, a 10 ms warm-up, and the identical deterministic policy list for all
four generators.

The measured platform is an RTX 5080, compute capability 12.0, UUID
`GPU-12fc46ca-1e10-bfc0-0997-84b0fa735418`, 17,094,475,776 bytes of VRAM,
NVIDIA KMD 610.62, CUDA driver API 13.3, and CUDA runtime 13.0 under WSL2. CUDA
uses the checksum-pinned 13.0.2 Bazel toolkit and the checksum-pinned hermetic
GCC 15.2 host compiler. Google Benchmark is the pinned 1.9.5 dependency.

## Decision

- Retain CPU A* as the production dispatch, correctness oracle, small-job
  fallback, and recovery path. Do not promote either CUDA generator.
- Retain parallel host CPU A* as the best measured batch implementation for
  several larger or denser cases, but do not add an automatic production
  crossover policy from this one-platform corpus.
- Keep CUDA frontier and sweep as explicit experimental forced backends for
  differential tests, replay, and future bakeoffs. They are not silent
  fallbacks or production defaults.
- Preserve both GPU algorithms. Sweep won 35 of 66 GPU-only
  execution/readback comparisons and frontier won 31; after complete
  end-to-end work, frontier won 38 GPU-only comparisons and sweep won 28. No
  single GPU algorithm dominates the measured corpus.
- Keep exact candidate admission and deterministic store publication outside
  trusted generator state. A GPU route matching CPU scalar cost remains
  untrusted until every exact admission invariant passes.

## Measured basis

- CPU won all 66 end-to-end corpus/pool-size combinations: sequential CPU A*
  won 52 and parallel CPU A* won 14. CUDA won none.
- With prepared upload and exact admission excluded, CUDA sweep won only four
  of 66 execution/readback comparisons. CUDA frontier won none against the
  fastest CPU mode.
- The closest end-to-end GPU result was cross-tile edges at `k=128`: CUDA
  frontier took 5.410 ms versus parallel CPU A* at 5.293 ms, 1.022 times the
  CPU latency. On the KiCad fixture at `k=128`, CUDA sweep took 34.052 ms versus
  parallel CPU A* at 7.962 ms, 4.277 times the CPU latency.
- Prepared flatten/upload medians ranged from 0.378 to 0.644 ms, with a median
  of 0.413 ms across the eleven cases. End-to-end GPU rows include this cost
  and prepared-view release.
- All 792 non-upload median generator/stage rows were ordered,
  deterministic, and matched CPU reachability/failure and optimal scalar cost.
  There were no backend, validation, invariant, resource-exhaustion,
  cancellation, unsupported, or invalid outcomes. The 455 unreachable policy
  queries in the unique 2,772-query matrix were reproduced by every backend.
- Peak backend-owned VRAM was 28,288,168 bytes on KiCad sweep at `k=128`.
  Peak deterministic GPU batch host payload was 45,047,240 bytes on KiCad
  `k=128`, tied by frontier and sweep.
- The policy schedule produced resource diversity but a flat intrinsic
  best-of-k curve in every reachable row. The base policy is already optimal
  under the reported intrinsic metric; the complete policy schedule produced
  pools of up to four unique retained candidates including the base geometry,
  but did not improve that metric.

## Consequences

- The central Phase 3 GPU-dispatch hypothesis is negative for this corpus,
  hardware, implementation, and `k <= 128`. Shared upload and higher device
  work throughput do not overcome batch setup, readback, exact admission, and
  the measured search shape end to end.
- The negative dispatch result does not invalidate batched GPU exploration:
  both kernels are deterministic, isolated, bounded, and differential-testable,
  and they provide a stable measurement platform for larger future searches.
- Larger boards, batches beyond 128, lower-readback designs, persistent kernels,
  or richer alternative policies may change a later crossover. Those are
  hypotheses, not measured Phase 3 conclusions.
- Exact through-via candidates, allocator pricing/worlds, selection, portals,
  legalization, and CAD commit transactions remain outside this decision.
  This ADR does not claim M1 completion.
