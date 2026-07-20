# Phase 3 Persistent Workspace and Compact Sweep Follow-up

## Status

Canonical measurement is pending from the final implementation snapshot.
Earlier exploratory numbers are intentionally not retained here because the
parallel scan/scatter compactor, bounded validation scratch, prepared-view
execution lease, and `k=256,512` rows all changed the measured path or matrix.

The publishable run will use a clean VCS-stamped build, retain the raw JSON and
SHA-256 manifest under `benchmarks/results/`, validate every CPU/GPU
differential and determinism counter, and distinguish statistically clear wins
from nominal median leads.

## Planned matrix

- Eleven versioned Phase 3 cases.
- Candidate counts `4,8,16,32,64,128,256,512`.
- Sequential CPU A*, parallel CPU A*, and compact CUDA sweep.
- `execution_readback` and `end_to_end` timing scopes.
- Twenty repetitions, 20 ms minimum sample time, and 10 ms warm-up.
- RTX 5080 / compute capability 12.0 with checksum-pinned hermetic CUDA and
  host compiler toolchains.

CPU A* remains production dispatch, correctness oracle, and fallback until the
canonical evidence supports an explicit workload-aware crossover rule.
