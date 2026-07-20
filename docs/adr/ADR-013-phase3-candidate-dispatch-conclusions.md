# ADR-013: Phase 3 Candidate Dispatch Conclusions

**Status:** Accepted
**Date:** July 20, 2026
**Applies to:** Phase 3 CPU/GPU candidate-generation dispatch and the retained
experimental CUDA generators

## Context

Phase 3 tests whether many independent detailed searches sharing one immutable
prepared CompiledBoard upload make GPU candidate exploration attractive. The
comparison must include reconstruction, exact Board IR admission, immutable
store publication, and upload cost rather than promote a backend from kernel
throughput alone. CPU A* remains the correctness oracle and production
dispatch unless reproducible end-to-end evidence supports a change.

The evidence is the Phase 3 result-schema v2 artifact produced by Google
Benchmark 1.9.5:
`benchmarks/results/phase3_candidate_bakeoff_3e3fe4c.json`, SHA-256
`4734665b627c8de7f719247ef463d9b6694502326caf993112aec848479e6e2d`,
built from exact source commit `3e3fe4cc064d73e0bd4e3cadc2bf5024494faaca`.
The canonical
`benchmarks/results/phase3_candidate_bakeoff_manifest_v1.json` binds the exact
bytes of that artifact, the human report, and this ADR with independent SHA-256
digests; validation checks those digests before recomputing the conclusions.
The artifact identifies its source through Bazel stable workspace status,
records a clean tree, and marks the checked-in canonical invocation as its
trust boundary; it does not accept a caller-selected commit label as evidence.
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
- Preserve both GPU algorithms. Frontier won 29 and sweep won 37 of 66
  GPU-only execution/readback comparisons; after complete end-to-end work,
  frontier won 40 GPU-only comparisons and sweep won 26. No single GPU
  algorithm dominates the measured corpus.
- Keep exact candidate admission and deterministic store publication outside
  trusted generator state. A GPU route matching CPU scalar cost remains
  untrusted until every exact admission invariant passes.

## Measured basis

- CPU won all 66 end-to-end corpus/pool-size combinations: sequential CPU A*
  won 52 and parallel CPU A* won 14. CUDA won none.
- With prepared upload and exact admission excluded, CUDA sweep won only four
  of 66 execution/readback comparisons against the fastest CPU mode; CUDA
  frontier nominally won one, overlapping sweep at cross-tile `k=128`. The
  frontier row's 13.7% median advantage was smaller than its 26.6% real-time
  coefficient of variation; the matching parallel CPU row's coefficient of
  variation was 5.4%.
- The closest end-to-end GPU result was cross-tile edges at `k=128`: CUDA
  frontier took 3.187 ms versus parallel CPU A* at 2.681 ms, 1.189 times the
  CPU latency. On the KiCad fixture at `k=128`, CUDA sweep took 25.214 ms versus
  parallel CPU A* at 5.532 ms, 4.558 times the CPU latency.
- Prepared flatten/upload medians ranged from 0.376 to 0.670 ms, with a median
  of 0.417 ms across the eleven cases. End-to-end GPU rows include this cost
  and prepared-view release.
- All 792 non-upload median generator/stage rows were ordered,
  deterministic, and matched CPU reachability/failure and optimal scalar cost.
  V2 ordered semantic outcome checksums independently agree across generators;
  all 2,202 structured store rejection records in the unique sequential-CPU
  evidence view and all 2,195 per GPU generator were retained.
  There were no backend, validation, invariant, resource-exhaustion,
  cancellation, unsupported, or invalid outcomes. The 455 unreachable policy
  queries in the unique 2,772-query matrix were reproduced by every backend.
- Peak backend-owned VRAM was 28,288,168 bytes on KiCad sweep at `k=128`.
  Peak deterministic GPU batch host payload was 22,564,808 bytes on KiCad
  `k=128`, tied by frontier and sweep. The prepared deterministic node-lookup
  index added at most 2,788 persistent host bytes on this corpus.
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
- Review-driven changes removed a second host partition copy from GPU
  readback, replaced repeated policy-resource node scans with a deterministic
  lookup, and batched candidate normalization and store publication. Relative
  to the preceding artifact, peak deterministic GPU host payload halved and
  the combined exact-admission/store measurement improved by about 12% across
  generators. GPU execution/readback latency did not improve uniformly, so
  these fixes do not change dispatch.
- Larger boards, batches beyond 128, lower-readback designs, persistent kernels,
  or richer alternative policies may change a later crossover. Those are
  hypotheses, not measured Phase 3 conclusions.
- Exact through-via candidates, allocator pricing/worlds, selection, portals,
  legalization, and CAD commit transactions remain outside this decision.
  This ADR does not claim M1 completion.
