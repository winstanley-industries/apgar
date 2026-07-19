# Planar Bakeoff Result Contract v1

The Phase 2 machine-readable result is Google Benchmark 1.9.5 JSON. APGAR adds
context keys prefixed `apgar_` for the exact commit, corpus version, timing and
warm-up policy, pinned CUDA/host toolchains, and backend/device metadata.
Each `apgar_case_<name>` value fixes the family, Board IR hash,
compiler-profile fingerprint, represented nodes and legal edges, lattice step,
tile dimensions, heading mask, and orthogonal/diagonal/bend costs.

Benchmark names are `<forced-generator>/<case>`, where the generators are
`cpu_astar`, `cuda_frontier`, and `cuda_sweep`. Google Benchmark owns iteration
selection, real-time measurement, warm-up, repetition, and aggregate
statistics. APGAR publishes domain counters for reachability, canonical failure
code, scalar cost, deterministic geometry fingerprint halves, examined work,
convergence rounds, kernel time, peak owned device bytes, determinism, and CPU
differential agreement. Peak owned bytes are APGAR allocations, not
whole-process or driver-pool usage.

Published artifacts contain Google Benchmark's mean, median, standard
deviation, and coefficient-of-variation aggregates for each forced
generator/case; per-repetition rows are deliberately omitted.

`differential_match` requires equal reachability/failure class and, when
reachable, equal optimal scalar cost. Geometry fingerprints need not agree
between generators. `deterministic` requires each forced generator's externally
visible result to be identical across timed iterations; a violation causes the
Google Benchmark run to report an error.
