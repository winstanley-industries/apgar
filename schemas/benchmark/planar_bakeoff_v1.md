# Planar Bakeoff Result Contract v1

The Phase 2 machine-readable result is Google Benchmark 1.9.5 JSON. APGAR adds
context keys prefixed `apgar_` for the exact commit, corpus version, timing and
warm-up policy, pinned CUDA/nvcc/benchmark C++ toolchains, and backend/device
metadata. The CPU A* baseline is part of the CUDA-linked benchmark executable,
so its C++ toolchain is the recorded benchmark GCC toolchain rather than the
repository's default CPU-only LLVM configuration.
The context also records the fixed no-RNG seed policy, CPU baseline, host OS,
kernel, architecture, CPU model, and component-level nvcc/cudart builds.
Each `apgar_case_<name>` value fixes the family, Board IR hash,
compiler-profile fingerprint, represented nodes and legal edges, lattice step,
tile dimensions, heading mask, and orthogonal/diagonal/bend costs.
Backend/device metadata is mandatory. The benchmark executable exits nonzero
without running benchmarks if it cannot query that metadata, so a successful
v1 artifact always contains the complete provenance context.

Benchmark names are `<forced-generator>/<case>`, where the generators are
`cpu_astar`, `cuda_frontier`, and `cuda_sweep`. Google Benchmark owns iteration
selection, real-time measurement, warm-up, repetition, and aggregate
statistics. Each corpus case is flattened and uploaded once before benchmark
timing; frontier and sweep reuse that immutable prepared view. The timed GPU
scope is route execution, readback, reconstruction, and full untrusted-result
validation. APGAR publishes domain counters for reachability, canonical failure
code, scalar cost, deterministic geometry fingerprint halves, examined work,
convergence rounds, kernel time, peak route-owned device bytes, determinism, and
CPU differential agreement. Peak route-owned bytes are that row's immutable
view plus route-batch allocations. They exclude driver/allocator-pool usage and
other corpus views resident in the benchmark harness.

The numeric `failure_code` mapping is stable: `0` reached, `1` invalid input,
`2` unsupported, `3` disconnected, `4` resource exhausted, `5` cancelled,
`6` backend failure, `7` validation failure, `8` internal invariant, and `9`
unknown/unclassified. Invalid input and an unknown future outcome therefore
remain distinguishable even though Google Benchmark counters are numeric.

Published artifacts contain Google Benchmark's mean, median, standard
deviation, and coefficient-of-variation aggregates for each forced
generator/case; per-repetition rows are deliberately omitted.

`differential_match` requires equal reachability/failure class and, when
reachable, equal optimal scalar cost. Geometry fingerprints need not agree
between generators. `deterministic` requires each forced generator's externally
visible result to be identical across timed iterations; a violation causes the
Google Benchmark run to report an error.
