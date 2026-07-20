# Phase 3 Persistent Workspace and Compact Sweep Follow-up

## Status

Canonical measurement is pending from the final implementation snapshot.
Earlier exploratory numbers are intentionally not retained here because the
parallel scan/scatter compactor, bounded validation scratch, prepared-view
execution lease, and `k=256,512` rows all changed the measured path or matrix.

The publishable run will use a clean VCS-stamped build, retain the raw JSON and
SHA-256 manifest under `benchmarks/results/`, validate every CPU/GPU
differential and determinism counter, and distinguish statistically clear wins
from nominal median leads. It uses result schema
`phase3_candidate_bakeoff_v3`, which adds the prepared full-pipeline timing
scope and the larger candidate-count matrix without changing the retained v2
artifact or validator.

## Planned matrix

- Eleven versioned Phase 3 cases.
- Candidate counts `4,8,16,32,64,128,256,512`.
- Sequential CPU A*, parallel CPU A*, and compact CUDA sweep.
- `execution_readback`, prepared-view `prepared_end_to_end`, and cold
  `end_to_end` timing scopes. Before the prepared-view row begins timing, one
  identical full-pipeline warm-up fills the sweep cache and verifies CPU
  differential/admission semantics. Its timed iterations include exact
  admission, a fresh bounded store, metrics, and transient result release;
  prepare/upload, warm-up, and retained-view release are excluded.
- Twenty repetitions, 20 ms minimum sample time, and 10 ms warm-up.
- RTX 5080 / compute capability 12.0 with checksum-pinned hermetic CUDA and
  host compiler toolchains.

CPU A* remains production dispatch, correctness oracle, and fallback until the
canonical evidence supports an explicit workload-aware crossover rule.
