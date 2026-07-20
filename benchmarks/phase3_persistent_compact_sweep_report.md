# Phase 3 Persistent Workspace and Compact Sweep Follow-up

## Outcome

Persistent bounded sweep workspaces, compact query-major readback, bounded
parallel host validation, and batch candidate conversion establish a measured
GPU throughput crossover for prepared GPU-heavy candidate exploration. Across
the complete matrix, `prepared_end_to_end` workload-pooled throughput is
1.021244910038941 times the faster CPU mode. Per candidate count it first
exceeds CPU at `k=64`, and its equal-row geometric-mean speedup is above one at
`k=256` and `k=512`.

This is not a universal latency or dispatch win. CUDA wins 27 of 88 prepared
median rows, while CPU wins 61; cold `end_to_end` workload-pooled throughput is
0.6913863827970889 times CPU. CPU A* therefore remains production dispatch,
correctness oracle, disconnected-result confirmation path, and fallback. A
future dispatch experiment may target sufficiently large compatible batches
on an already prepared view.

## Evidence binding

- Result: `benchmarks/results/phase3_persistent_compact_sweep_c68829c.json`
- Result SHA-256:
  `f766e267cfd5ab740a0bb4980cd75ae74e995e0c2128a297b7b5ab39328b0783`
- Exact measured source commit:
  `c68829c62db8c01bee81c30d106adf4ff599ae43`
- Hardware: NVIDIA GeForce RTX 5080, compute capability 12.0, KMD driver
  610.62; AMD Ryzen 9 9950X3D.
- Toolchains: checksum-pinned hermetic CUDA 13.0.2 / GCC 15.2.0 benchmark
  toolchain and hermetic LLVM 22.1.8 default toolchain.
- Method: eleven versioned cases, candidate counts
  `4,8,16,32,64,128,256,512`, twenty repetitions, 20 ms minimum sample time,
  10 ms warm-up, and four aggregate rows per run.

The strict v3 validator derives and binds these compact JSON tokens:

```text
{"generator_stage_medians":792,"reached_queries":9331,"unexpected_failure_queries":0,"unique_requested_queries":11220,"unreachable_queries":1889}
{"end_to_end":{"clear_cpu_mean_wins":81,"clear_cuda_mean_wins":7,"comparisons":88,"equal_row_geomean_cuda_speedup":0.30946512503839624,"nominal_cpu_median_wins":81,"nominal_cuda_median_wins":7,"nominal_median_ties":0,"pooled_cuda_throughput_speedup":0.6913863827970889,"unclear_mean_differences":0},"execution_readback":{"clear_cpu_mean_wins":62,"clear_cuda_mean_wins":23,"comparisons":88,"equal_row_geomean_cuda_speedup":0.36894891000697294,"nominal_cpu_median_wins":62,"nominal_cuda_median_wins":26,"nominal_median_ties":0,"pooled_cuda_throughput_speedup":0.9505446967777404,"unclear_mean_differences":3},"prepared_end_to_end":{"clear_cpu_mean_wins":56,"clear_cuda_mean_wins":27,"comparisons":88,"equal_row_geomean_cuda_speedup":0.5103362865877366,"nominal_cpu_median_wins":61,"nominal_cuda_median_wins":27,"nominal_median_ties":0,"pooled_cuda_throughput_speedup":1.021244910038941,"unclear_mean_differences":5}}
```

## Correctness and accounting

All 792 generator/stage median rows are externally ordered, deterministic, and
match the sequential CPU oracle for per-policy reachability, failure class,
and scalar cost. The unique 11,220-query matrix contains 9,331 reached queries
and 1,889 intentionally unreachable queries. No row reports invalid,
unsupported, resource-exhausted, cancelled, backend, validation, or invariant
failure. Prepared and cold admission results repeat exactly within each
generator.

The validator also checks the exact 3,212 aggregate-row matrix with no extras,
the clean source stamp, four fixed sweep launches, round and blocking-readback
identities, compact path byte bounds, persistent-plus-capacity VRAM peaks,
bounded validation worker counts, and the full-batch candidate-adapter index
in deterministic host-payload accounting.

## Throughput results

The primary decision scope is `prepared_end_to_end`: one identical untimed
full-pipeline warm-up fills the workspace and passes differential/admission
checks, then timed iterations include generation, compact readback, hostile
output validation, exact candidate admission, a fresh bounded Candidate Store,
quality metrics, and transient-result release. Prepare/upload, warm-up, and
retained-view release are excluded.

| k | CUDA median wins | clear CUDA mean wins | equal-row GM speedup | pooled throughput speedup |
|---:|---:|---:|---:|---:|
| 4 | 0/11 | 0/11 | 0.112x | 0.195x |
| 8 | 1/11 | 1/11 | 0.204x | 0.329x |
| 16 | 2/11 | 2/11 | 0.365x | 0.538x |
| 32 | 3/11 | 3/11 | 0.630x | 0.879x |
| 64 | 4/11 | 4/11 | 0.853x | 1.066x |
| 128 | 7/11 | 7/11 | 0.999x | 1.181x |
| 256 | 5/11 | 5/11 | 1.017x | 1.149x |
| 512 | 5/11 | 5/11 | 1.009x | 1.122x |

The pooled calculation gives every submitted candidate equal weight:
`sum(fastest CPU median time) / sum(CUDA median time)`. The equal-row geometric
mean gives each case/count row equal weight. Values above one favor CUDA.
Dense corridors (1.327x), the KiCad fixture (2.017x), negative coordinates
(1.213x), and cross-tile edges (1.016x) have prepared pooled wins across all
eight counts. Disconnected fields remain strongly CPU-favored because every
CUDA-disconnected result still requires exact CPU-oracle confirmation.

Generator-only `execution_readback` pooled throughput crosses at `k=64`, rises
to 1.312x at `k=256`, and is 1.227x at `k=512`; its equal-row geometric mean is
above one only at `k=256`. Cold `end_to_end` never crosses in this matrix: its
best pooled result is 0.853x at `k=512` because each iteration recreates and
releases the prepared view and workspace.

The aggregate-only normal approximation classifies a CUDA mean win when
`cuda_mean + 1.96 * sqrt((cuda_stddev^2 + cpu_stddev^2) / 20)` is below both
CPU means. It is not a paired-trial analysis or median confidence interval.
Prepared CUDA timing is the stable decision signal: median CV is 2.27%, the
maximum is 5.38%, and only 2 of 88 rows exceed 5%. Execution-only CUDA timing
is noisy (median CV 14.53%, 87 of 88 rows above 5%, KiCad `k=512` maximum
117.02%), so its nominal medians must not be read without the mean-difference
qualification.

## Improvement over the accepted v2 implementation

Against the canonical v2 artifact at commit `3e3fe4c` over the common eleven
cases and `k=4..128`, CUDA sweep is 1.469x faster by equal-row geometric mean
for `execution_readback` (59 of 66 rows improved) and 1.252x faster for cold
`end_to_end` (63 of 66 improved). CPU controls changed by only 1.008x/1.015x
for execution and 1.003x/1.016x for cold sequential/parallel modes. The
reported GPU batch host workspace is 4.592x smaller by geometric mean, with a
3.05x to 6.84x row range. The v2 artifact did not expose exact final D2H bytes,
so no retroactive transfer-byte ratio is claimed.

## Decision

The implementation is accepted as the Phase 3 GPU-heavy batch exploration
path. It satisfies the objective only in the prepared, sufficiently large,
workload-aware regime; it does not replace CPU A* globally. The next dispatch
slice should classify prepared-view reuse, batch size, disconnected-policy
likelihood, and compatible corpus shape, then publish separate evidence before
changing the production default.
