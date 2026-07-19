# ADR-010: Phase 2 Planar GPU Bakeoff and Dispatch

**Status:** Accepted
**Date:** July 18, 2026
**Applies to:** Planar route-generation backends and current dispatch policy

## Context

The architecture calls for a deterministic GPU route generator, CPU/GPU
differential evidence, and a benchmark-driven dispatch decision. Phase 2 is
restricted to the conservative planar H/V/45-degree `CompiledBoard` fields from
Phase 1. Exact through-via transitions and the Phase 3 candidate store do not
yet exist.

CUDA kernels also require a host compiler. Depending on a system CUDA Toolkit
or system GCC would violate the repository's toolchain-hermeticity boundary and
make the supported device build difficult to reproduce.

## Decision

- The CUDA configuration pins CUDA Toolkit 13.0.2 redistributable components
  and a GCC 15.2.0 compiler/sysroot by checksum. It does not discover CUDA or
  GCC from the host, and pinned libstdc++/libgcc are statically linked rather
  than loaded from `/usr`. CPU-only Bazel builds remain the default on
  supported Linux and macOS hosts.
- The backend boundary operates on compiled-view upload, route execution,
  reconstruction, telemetry, replay, and structured failures. It does not
  expose one-for-one CUDA API wrappers. CUDA is the only implemented GPU
  backend in Phase 2.
- Device CompiledBoard schema v1 is immutable and versioned. It carries stable
  node and incoming-heading state indices, explicit array bounds, association
  fingerprints, and deterministic owned-byte accounting. A prepared-view API
  permits repeated route execution against one validated immutable upload.
- The deterministic bucketed frontier and heading-aware segmented sweep remain
  experimental planar generators. Every result is reconstructed and validated
  on the host; neither generator is legality authority.
- Current production dispatch remains CPU A*. On the Phase 2 corpus, CPU A*
  has the lowest full-route median time in all eight cases. Sweep has the lowest
  time of the two GPU prototypes in all eight cases and is the preferred GPU
  research path. Frontier remains a useful correctness and contention stress
  prototype.
- Dispatch measurements use the pinned Google Benchmark 1.9.5 module. The
  published evidence is the aggregate-only Google Benchmark JSON artifact and
  its human-readable report; APGAR does not implement a parallel timing or
  statistics framework.

## Consequences

- A CUDA build is reproducible without a system CUDA Toolkit or system GCC,
  while execution still requires the declared host glibc ABI plus a compatible
  NVIDIA kernel driver and GPU.
- The benchmark result is deliberately negative: the current GPU prototypes do
  not justify dispatching these small and medium planar fields away from CPU A*.
- Sweep is not promoted to a general speed winner. Candidate batching, reuse,
  larger fields, and later candidate-store integration require new evidence.
- Both CUDA generators must continue to match CPU reachability/failure
  semantics and optimal scalar cost, remain deterministic on the supported
  backend/device class, and pass hostile-result validation and replay tests.
- This decision does not complete M1. Through vias, the Phase 3 candidate
  store, and all other deferred routing features remain out of scope.

## Evidence

The decision is based on commit
`9ff2f9143c78774218bdda4e9900b27823a25d5c`, corpus v1, an NVIDIA GeForce RTX
5080 (compute capability 12.0), and the fixed warm-up/repetition policy recorded
in `benchmarks/phase2_planar_dispatch_report.md` and
`benchmarks/results/phase2_planar_bakeoff_9ff2f91.json`.
