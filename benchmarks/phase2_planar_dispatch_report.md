# Phase 2 Planar GPU Dispatch Report

## Result

Keep CPU A* as the default dispatcher for the current conservative planar
fields. CUDA sweep is the better of the two GPU prototypes in all eight corpus
cases, but CPU A* has the lowest full-route median in every case. CUDA frontier
remains useful as a deterministic correctness and contention-stress prototype.

This is a correct negative performance result. It does not claim that a GPU
cannot win on later, larger, reused, or batched workloads.

## Reproduction identity

- APGAR commit: `9ff2f9143c78774218bdda4e9900b27823a25d5c`
- Corpus/schema: planar bakeoff v1
- Host: Ubuntu 26.04 under WSL2, x86-64, kernel
  `6.18.33.2-microsoft-standard-WSL2`
- CPU: AMD Ryzen 9 9950X3D, 16 cores/32 threads, 96 MiB L3
- GPU: NVIDIA GeForce RTX 5080, UUID
  `GPU-12fc46ca-1e10-bfc0-0997-84b0fa735418`, compute capability 12.0,
  17,094,475,776 bytes global memory
- NVIDIA Windows KMD reported by `nvidia-smi`: 610.62
- CUDA driver API/runtime: 13030/13000
- CUDA build inputs: checksum-pinned CUDA Toolkit 13.0.2 redistributables and
  nvcc build 13.0.88 and cudart build 13.0.96, plus a checksum-pinned GCC
  15.2.0 compiler/sysroot; neither comes from the host
- Benchmark C++ toolchain: the same checksum-pinned GCC 15.2.0 distribution
  builds the CPU A* baseline, CUDA host code, and Google Benchmark in this
  single CUDA-linked executable. Its pinned libstdc++ and libgcc are linked
  statically and verified absent from the executable's dynamic dependencies.
  Default CPU-only APGAR builds use pinned LLVM 22.1.8 with libc++.
- Benchmark framework: pinned Google Benchmark 1.9.5, release build
- Determinism seed: none; corpus v1 is fixed and contains no RNG

The exact command was:

```sh
bazel run --config=cuda --config=benchmark //:planar_benchmark -- \
  --apgar_commit=9ff2f9143c78774218bdda4e9900b27823a25d5c \
  --benchmark_out=/home/adam/code/apgar/benchmarks/results/phase2_planar_bakeoff_9ff2f91.json \
  --benchmark_out_format=json \
  --benchmark_format=console
```

The machine-readable result is
`benchmarks/results/phase2_planar_bakeoff_9ff2f91.json`. It records all hardware,
backend, toolchain, corpus, and policy fields required by
`schemas/benchmark/planar_bakeoff_v1.md`.

## Measurement policy

Google Benchmark owns iteration selection, warm-up, timing, repetitions, and
aggregate statistics. Each forced generator/case uses real time in
microseconds, a 0.02-second minimum measurement interval, a 0.01-second warm-up,
and 20 repetitions. The artifact publishes mean, median, standard deviation,
and coefficient of variation only. The timed scope is a complete route call,
including CompiledBoard upload, kernel execution, reconstruction, and untrusted
result validation.

APGAR contributes domain counters, not an independent timing framework:
reachability, failure class, optimal scalar cost, deterministic geometry hash,
examined work, convergence rounds, kernel time, and peak APGAR-owned device
bytes. Owned bytes exclude CUDA driver and allocator-pool overhead.

## Corpus configuration

| Case | Nodes | Directed edges | Step | Tile | Headings | Orthogonal/diagonal/bend cost |
|---|---:|---:|---:|---:|---:|---:|
| dense corridors | 231 | 1,660 | 5 | 8x8 | H/V/45 | 7/11/13 |
| sparse regions | 11 | 20 | 10 | 4x3 | H/V/45 | 19/3/29 |
| fragmented runs | 11 | 20 | 10 | 5x2 | H/V/45 | 17/23/31 |
| high-turn maze | 35 | 68 | 10 | 3x3 | H/V | 10/14/101 |
| cross-tile edges | 33 | 184 | 10 | 3x2 | H/V/45 | 5/8/2 |
| negative coordinates | 75 | 484 | 10 | 4x3 | H/V/45 | 37/41/43 |
| disconnected fields | 10 | 16 | 10 | 4x2 | H/V | 10/14/3 |
| KiCad fixture | 697 | 4,136 | 500,000 | 8x8 | H/V/45 | 1,000/1,414/100 |

The JSON context additionally records the Board IR hash and compiler-profile
fingerprint for every case.

## Median results

Full-route time and kernel time are milliseconds. Work is expanded CPU states
or examined GPU state/edge work. VRAM is peak APGAR-owned device bytes.

| Case | Generator | Full route ms | Kernel ms | Work | Rounds | VRAM bytes | Reach/cost |
|---|---|---:|---:|---:|---:|---:|---:|
| dense corridors | CPU A* | 0.014 | - | 20 | - | - | yes/140 |
|  | CUDA frontier | 4.937 | 4.097 | 12,385 | 29 | 64,268 | yes/140 |
|  | CUDA sweep | 1.428 | 0.617 | 6,640 | 4 | 62,424 | yes/140 |
| sparse regions | CPU A* | 0.003 | - | 14 | - | - | yes/149 |
|  | CUDA frontier | 3.419 | 2.679 | 39 | 20 | 3,036 | yes/149 |
|  | CUDA sweep | 1.629 | 0.897 | 120 | 6 | 2,952 | yes/149 |
| fragmented runs | CPU A* | 0.003 | - | 16 | - | - | yes/454 |
|  | CUDA frontier | 2.544 | 1.809 | 39 | 13 | 3,196 | yes/454 |
|  | CUDA sweep | 2.268 | 1.574 | 220 | 11 | 3,112 | yes/454 |
| high-turn maze | CPU A* | 0.011 | - | 62 | - | - | yes/1,148 |
|  | CUDA frontier | 8.783 | 8.081 | 135 | 61 | 9,052 | yes/1,148 |
|  | CUDA sweep | 2.304 | 1.538 | 748 | 11 | 8,776 | yes/1,148 |
| cross-tile edges | CPU A* | 0.007 | - | 10 | - | - | yes/50 |
|  | CUDA frontier | 2.691 | 1.935 | 1,117 | 14 | 9,908 | yes/50 |
|  | CUDA sweep | 1.424 | 0.655 | 736 | 4 | 9,648 | yes/50 |
| negative coordinates | CPU A* | 0.007 | - | 10 | - | - | yes/370 |
|  | CUDA frontier | 3.083 | 2.291 | 3,340 | 16 | 21,596 | yes/370 |
|  | CUDA sweep | 1.452 | 0.673 | 1,936 | 4 | 21,000 | yes/370 |
| disconnected fields | CPU A* | 0.001 | - | 9 | - | - | no/disconnected |
|  | CUDA frontier | 1.690 | 0.921 | 15 | 6 | 2,720 | no/disconnected |
|  | CUDA sweep | 1.284 | 0.537 | 48 | 3 | 2,644 | no/disconnected |
| KiCad fixture | CPU A* | 0.371 | - | 758 | - | - | yes/37,168 |
|  | CUDA frontier | 7.014 | 5.954 | 30,752 | 43 | 188,692 | yes/37,168 |
|  | CUDA sweep | 1.873 | 0.898 | 24,816 | 6 | 183,120 | yes/37,168 |

All 24 forced generator/case combinations reported deterministic results.
Both CUDA generators matched CPU reachability/failure semantics and optimal
scalar cost in every case. No benchmark failures occurred. The disconnected
case consistently reported canonical failure code 3 rather than resource or
round-budget exhaustion.

On the largest current fixture, CPU A* is about 5.0 times faster than sweep and
18.9 times faster than frontier by full-route median; sweep is about 3.7 times
faster than frontier. Upload and host validation dominate enough of these
small-to-medium workloads that kernel-only timing is not a valid dispatch
metric.

## Scope and dispatch conclusion

Use CPU A* by default for the present Phase 2 planar fields. Retain CUDA sweep
as the preferred experimental GPU path for larger-field and future batching
research, and retain frontier for differential and stress coverage. Re-run the
bakeoff before changing dispatch policy, using the exact commit, corpus,
configuration, hardware, and baseline identity required by the result schema.

This report covers planar H/V/45 movement only. It neither implements nor
claims M1 through vias, Phase 3 candidate storage, allocator worlds/pricing,
portals, distance fields, incremental invalidation, legalization,
topology-first routing, HIP, Metal, Vulkan, or specialty routing.
