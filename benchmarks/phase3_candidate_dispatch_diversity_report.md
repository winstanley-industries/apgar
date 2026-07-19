# Phase 3 Candidate Dispatch and Diversity Report

## Outcome

CPU A* remains APGAR's production candidate-generation dispatch. On the
measured RTX 5080 platform, CPU won every one of the 66 end-to-end
corpus/pool-size comparisons. Batched CUDA sweep won four execution-only rows
after prepared upload and exact admission were excluded; neither GPU generator
won an end-to-end row.

This is a valid negative Phase 3 result. The immutable candidate contract,
exact admission pipeline, deterministic store, shared alternative policies,
and both isolated CUDA batch explorers remain useful and tested, but the
evidence does not justify GPU promotion.

## Evidence identity

| Field | Recorded value |
| --- | --- |
| Source commit | `eee37945ba005e535c6dde549e083ffca7cd249d` |
| Machine result | `benchmarks/results/phase3_candidate_bakeoff_eee3794.json` |
| Result SHA-256 | `2e8e4beff7b1e6f281f5b170e9f7253403925a013e95a620f888f8395f0a6462` |
| Result schema | `phase3_candidate_bakeoff_v1` |
| Corpus | 11 versioned cases; dense, sparse, fragmented, high-turn, cross-tile, negative-coordinate, disconnected, symmetric, multi-channel bottleneck, policy-alternative, and KiCad |
| Requested candidates per net | 4, 8, 16, 32, 64, 128 |
| Policy schedule | base, length, bend, strong resource penalty, resource ban v1 |
| Repetitions | 20; minimum 20 ms per repeat; 10 ms warm-up |
| Reported time | median real time; all rows use microseconds |
| Parallel CPU workers | 32 logical workers maximum |

The exact reproduction command was:

```sh
bazel run --config=cuda --config=benchmark \
  --define=APGAR_COMMIT=eee37945ba005e535c6dde549e083ffca7cd249d \
  //:phase3_candidate_benchmark -- \
  --apgar_commit=eee37945ba005e535c6dde549e083ffca7cd249d \
  --apgar_nvidia_kmd_driver=610.62 \
  --benchmark_out=/home/adam/code/apgar/benchmarks/results/phase3_candidate_bakeoff_eee3794.json \
  --benchmark_out_format=json --benchmark_format=console
```

## Platform and hermetic toolchains

| Component | Recorded value |
| --- | --- |
| Host | AMD Ryzen 9 9950X3D, Ubuntu 26.04 LTS under WSL2, Linux 6.18.33.2, x86-64 |
| GPU | NVIDIA GeForce RTX 5080, UUID `GPU-12fc46ca-1e10-bfc0-0997-84b0fa735418`, compute capability 12.0 |
| GPU memory | 17,094,475,776 bytes |
| NVIDIA KMD | 610.62, recorded from `nvidia-smi` |
| CUDA versions | driver API 13.3 (`13030`), runtime 13.0 (`13000`) |
| CUDA toolkit | pinned 13.0.2 manifest, SHA-256 `fce66717a81c510ffeb89ecc3e79849ab34af3b80139f750876d9033e31d71c2` |
| CUDA compiler | pinned nvcc 13.0.88, SHA-256 `48e35be3cfbf4b4fbc16828eaec8a7048ee789403049dc409f7b643d6259cf7a` |
| CUDA runtime | pinned cudart 13.0.96, SHA-256 `25b8071951baba827be1580b841d363464f6ee6c39f48d33a81646f90cc95ed1` |
| CUDA host C++ | pinned GCC 15.2 archive/sysroot, SHA-256 `ed6a74810fe42979493f3b0ef188f1b7a388817f496f4c9cee3f7183415ab821` |
| Default CPU C++ | LLVM 22.1.8 through `toolchains_llvm` 0.8.11 zero-sysroot |
| Benchmark library | pinned Google Benchmark 1.9.5 |
| Bazel | 9.2.0 through Bazelisk 1.29.0 |

The benchmark JSON embeds the hardware, OS, driver, CUDA, C++ toolchain, and
Google Benchmark fields. Bazel/Bazelisk versions and the following dynamic
dependency result come from the separate final validation recorded alongside
this artifact.

Dynamic dependency inspection found the pinned Bazel cudart through the target
RPATH and no dynamic `libstdc++` or `libgcc_s`. The driver-facing OS C runtime
libraries remain ordinary platform dependencies; no system CUDA, system nvcc,
or system GCC was used.

## Measurement scopes

- `prepared_upload` measures board flattening and upload; prepared-view release
  is excluded. Its median across cases was 0.413 ms, with a 0.378-0.644 ms
  range.
- `execution_readback` reuses one prepared upload. CPU rows measure CPU search;
  GPU rows measure batched execution, readback, reconstruction, and untrusted
  result validation.
- `exact_admission_store` measures exact Board IR validation, resource and
  metric reconstruction, immutable store admission, and the benchmark's
  diversity/checksum/statistics pass. It is deliberately not presented as a
  pure validator-only timer.
- `end_to_end` includes generation, exact admission, store publication, and
  metrics. GPU rows additionally include flattening, upload, result/prepared
  teardown, and release.

`candidate_queries_per_second` counts requested policy queries;
`generated_routes_per_second` counts reachable generated routes; and
`accepted_candidates_per_second` counts candidates retained after exact
admission and collision-safe deduplication. The JSON records all three.
Process peak RSS is a process-lifetime high-water mark and is not attributable
to one row. GPU memory is backend-owned prepared plus batch memory and excludes
driver or allocator-pool overhead. Generator-specific examined-state and work
units are diagnostic and are not compared as if they were identical work.

## Correctness and determinism

The artifact contains 3,212 aggregate rows: mean, median, standard deviation,
and coefficient of variation for 803 registered benchmarks. The 803 medians
comprise 11 prepared-upload rows and 792 generator/stage rows.

- All 792 generator/stage medians report deterministic ordered results and CPU
  differential agreement.
- The unique 66-case/pool policy matrix contains 2,772 requested queries:
  2,317 reachable and 455 intentionally unreachable due to disconnected fields
  or policy bans. Every generator matched those outcomes.
- No row reports backend failure, invalid input, invariant failure, validation
  failure, resource exhaustion, cancellation, or unsupported behavior.
- Every reachable result matches CPU optimal scalar cost under the identical
  policy. Equal-cost geometry is allowed to differ by backend.

## Dispatch results

End-to-end winners by pool size:

| Requested k | Sequential CPU wins | Parallel CPU wins | CUDA wins |
| ---: | ---: | ---: | ---: |
| 4 | 10 | 1 | 0 |
| 8 | 9 | 2 | 0 |
| 16 | 9 | 2 | 0 |
| 32 | 9 | 2 | 0 |
| 64 | 8 | 3 | 0 |
| 128 | 7 | 4 | 0 |
| **Total** | **52** | **14** | **0** |

With upload and admission excluded, CUDA sweep won only these four
execution/readback comparisons against both CPU modes:

| Case | k | Sweep | Next CPU | Sweep advantage |
| --- | ---: | ---: | ---: | ---: |
| Cross-tile edges | 64 | 0.988 ms | 0.992 ms sequential | 1.004x |
| Cross-tile edges | 128 | 1.368 ms | 1.609 ms parallel | 1.176x |
| Negative coordinates | 32 | 0.976 ms | 1.277 ms sequential | 1.308x |
| Negative coordinates | 64 | 1.340 ms | 1.588 ms parallel | 1.185x |

At `k=128`, the best CPU and best GPU end-to-end results were:

| Case | Best CPU | CPU latency | Queries/s | Best GPU | GPU latency | Queries/s | GPU / CPU latency |
| --- | --- | ---: | ---: | --- | ---: | ---: | ---: |
| Cross-tile edges | parallel | 5.293 ms | 24,184 | frontier | 5.410 ms | 23,662 | 1.022x |
| Dense corridors | parallel | 5.582 ms | 22,931 | sweep | 17.531 ms | 7,302 | 3.141x |
| Disconnected fields | sequential | 0.186 ms | 687,653 | frontier | 1.252 ms | 102,238 | 6.726x |
| Fragmented runs | sequential | 3.069 ms | 41,711 | frontier | 4.156 ms | 30,800 | 1.354x |
| High-turn maze | sequential | 4.318 ms | 29,643 | sweep | 5.749 ms | 22,266 | 1.331x |
| KiCad fixture | parallel | 7.962 ms | 16,075 | sweep | 34.052 ms | 3,759 | 4.277x |
| Multi-channel bottleneck | sequential | 4.289 ms | 29,846 | frontier | 5.677 ms | 22,546 | 1.324x |
| Negative coordinates | parallel | 5.355 ms | 23,903 | frontier | 6.758 ms | 18,941 | 1.262x |
| Policy alternatives | sequential | 4.923 ms | 26,001 | frontier | 6.151 ms | 20,810 | 1.249x |
| Sparse regions | sequential | 2.584 ms | 49,528 | sweep | 4.071 ms | 31,440 | 1.575x |
| Symmetric dual corridor | sequential | 4.371 ms | 29,284 | frontier | 5.977 ms | 21,416 | 1.367x |

The median of the eleven per-case fastest-GPU/fastest-CPU latency ratios at
`k=128` was 1.354, with a 1.022-6.726 range. The evidence therefore does not
support dispatching to the GPU merely because a subset of prepared executions
has higher throughput.

Batching does narrow the gap. Geometric-mean end-to-end latency ratios against
the fastest CPU mode in each matched case were:

| k | CUDA frontier / CPU | CUDA sweep / CPU |
| ---: | ---: | ---: |
| 4 | 26.398x | 19.686x |
| 8 | 13.993x | 10.745x |
| 16 | 7.551x | 5.901x |
| 32 | 4.076x | 3.447x |
| 64 | 2.921x | 2.530x |
| 128 | 1.991x | 1.929x |

This favorable scaling supports continued GPU research, but it is not a
measured crossover.

CUDA sweep was not uniformly better than frontier. Sweep won 35 of 66
GPU-only execution/readback comparisons and frontier won 31. Complete
end-to-end work reversed the count: frontier won 38 GPU-only comparisons and
sweep won 28. Both remain experimental forced backends.

## Performance interpretation: why CUDA did not cross over

The negative dispatch result is not evidence that the RTX 5080 performed route
propagation slowly in every case. In several sweep rows, the CUDA execution
envelope was much shorter than either the complete execution/readback stage or
the best parallel CPU search. The current correctness-first pipeline loses that
device-side advantage while allocating, transferring, partitioning,
reconstructing, and validating complete query workspaces.

Representative `k=128` medians are:

| Case and GPU generator | CUDA event envelope | GPU execution/readback | Approximate time outside event | Exact admission/store | GPU end to end | Best CPU end to end |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Dense corridors, sweep | 0.385 ms | 11.097 ms | 10.712 ms | 3.221 ms | 17.531 ms | 5.582 ms |
| KiCad fixture, sweep | 0.720 ms | 29.195 ms | 28.474 ms | 3.637 ms | 34.052 ms | 7.962 ms |
| Cross-tile edges, frontier | 0.619 ms | 1.823 ms | 1.204 ms | 3.138 ms | 5.410 ms | 5.293 ms |

The outside-event values are explanatory differences between independently
registered aggregate medians, not additive profiler attribution. The CUDA
event itself covers initialization, search chunks, blocking status copies,
finalization when needed, and predecessor selection. It excludes per-batch
allocation and input upload, complete result readback, host reconstruction and
validation, and teardown. No Nsight occupancy, transfer-bandwidth, or stall
profile was recorded, so the following explanations combine measured stage
envelopes with directly inspected implementation structure.

### Source-supported explanations

1. A prepared upload shares immutable CompiledBoard nodes and directional runs,
   but not transient query state. Every batch separately allocates queries,
   policies, result headers, labels, predecessors, two ownership arrays,
   status, and generator-specific frontier or sweep storage. Those allocations
   and their release are included in execution wall time but begin before and
   end after the CUDA event.
2. Every query returns its complete label, predecessor, state-owner, and
   predecessor-owner arrays. Readback first owns flat batch arrays and then
   copies them into query-local vectors, which is why the deterministic host
   formula contains two complete state workspaces. The dense `k=128` batch
   accounts for 14,984,648 host bytes; KiCad accounts for 45,047,240 bytes.
3. The host treats those arrays as hostile. It checks batch/query associations,
   ownership of every state, telemetry bounds, every finite predecessor and
   transition cost, stable goal selection, and reconstructed geometry before
   exact candidate admission. This trust boundary is required; transferring
   and materializing the entire workspace is an implementation choice.
4. The frontier assigns one 256-thread block to each query. For each A*-style
   round, that block scans the query's complete state slice, performs a stable
   minimum reduction, and uses one thread to relax the winning state's legal
   edges. It prioritizes bounded deterministic behavior over a compact active
   frontier. It also blocks for query-status readback after each 32-round
   chunk: dense `k=128` required 19 such readbacks and KiCad required 58.
5. Sweep exposes substantially more regular parallel work, but one eight-round
   chunk still uses initialization, four launches per round on boards with
   runs, one blocking status readback, and final predecessor selection. Its 34
   launches completed quickly in the two examples above; full-workspace
   handling, rather than propagation, dominated their wall time.
6. Exact Board IR admission and deterministic store publication are mandatory
   backend-neutral work. They create an end-to-end floor even when GPU search
   becomes faster. Prepared flatten/upload adds another 0.413 ms at the median
   measured case, although execution/readback rows already prove upload is not
   the only missing crossover cost.
7. Much of the generated work did not become stored value. Across the six CPU
   policy prefixes, 2,317 reachable routes produced 115 retained candidates.
   At KiCad `k=128`, all 128 queries reached but only two candidates survived
   exact admission, collision-safe deduplication, and retention. CPU generation
   also pays for discarded alternatives, but it returns compact routes rather
   than a complete accelerator workspace for each one.

Batching nevertheless worked in the intended direction. The geometric-mean
frontier and sweep end-to-end ratios improved from 26.398x and 19.686x at
`k=4` to 1.991x and 1.929x at `k=128`. This is evidence of amortization, but no
measured crossover through `k=128`.

### Prioritized future hypotheses

These are unmeasured hypotheses, not dispatch conclusions:

1. **Compact sweep output.** Reconstruct a bounded compact path on the device
   and read back query headers plus path primitives instead of four complete
   state arrays. CPU exact geometry, cost, resource, and candidate admission
   remain authoritative. A GPU `Unreachable` result still requires a CPU oracle
   confirmation or another independently validated proof; compact output must
   not weaken the trust boundary.
2. **Persistent batch workspaces.** Reuse capacity-bounded labels, ownership,
   status, and sweep buffers across batches sharing one prepared view. Measure
   allocation, input encoding/upload, device execution, output transfer, host
   validation, reconstruction, and release as separate Google Benchmark
   scopes before attributing improvement.
3. **Fewer host boundaries.** Evaluate a persistent sweep kernel, CUDA Graph,
   or another deterministic chunk schedule that reduces launches and blocking
   status copies while preserving bounded cancellation and query-local partial
   failure semantics.
4. **A real compact frontier.** Replace full-state winner scans with the
   architecture's intended stable integer buckets, delta-stepping, or bounded
   multi-queue experiment. Match the CPU policy-aware admissible heuristic more
   closely and retain stable external ordering and scalar-cost differential
   checks.
5. **Larger multi-net batches.** Test hundreds or thousands of compatible
   independent net/policy queries against one prepared board rather than only
   one net's `k <= 128` alternatives. This remains candidate generation; it
   does not introduce Phase 4 prices, worlds, or allocation.
6. **Pipeline overlap.** Overlap exact CPU admission of batch N with GPU
   exploration of batch N+1. Report latency and throughput separately so
   overlap does not hide upload, validation, or admission work.
7. **Higher-value policy schedules.** Improve resource-distinct candidate yield
   using the measured diversity feedback. A future header-first transfer may
   use untrusted signatures only to prioritize detailed readback; a signature
   match must never suppress a candidate. Canonical material still has to reach
   the CPU for collision-safe equality and exact admission before any omission.

The first follow-up vertical slice should combine persistent bounded workspaces
with compact sweep readback. Sweep already provides the strongest measured
device-side signal, and that slice directly attacks the largest observed gap
without changing production dispatch or exact legality authority. Any future
promotion still requires a new exact-commit bakeoff over the complete latency,
throughput, quality, memory, determinism, and failure-semantics gates.

## Candidate admission and diversity

The following totals use the sequential CPU end-to-end view so identical
backend and timing-stage repetitions are not counted repeatedly. Requested,
reachable, and unreachable counts are generator-independent; accepted values
are CPU sums of the per-case immutable pools after exact admission and
deduplication.

| Requested k | Requested queries | Reachable | Unreachable | Accepted unique CPU candidates |
| ---: | ---: | ---: | ---: | ---: |
| 4 | 44 | 40 | 4 | 15 |
| 8 | 88 | 75 | 13 | 16 |
| 16 | 176 | 148 | 28 | 18 |
| 32 | 352 | 296 | 56 | 19 |
| 64 | 704 | 586 | 118 | 23 |
| 128 | 1,408 | 1,172 | 236 | 24 |
| **Total** | **2,772** | **2,317** | **455** | **115** |

Within each CPU pool, all 115 accepted candidates had unique geometry and
resource signatures. The other 2,202 reachable CPU candidates became
structured store rejection records after collision-safe deduplication and
retention; there were no candidate-builder rejections. CUDA retained 122
candidates across the same six k prefixes and recorded 2,195 store rejections,
because it found one extra geometry in seven intermediate pools. The aggregate
artifact does not expose the rejection-code mix, so it does not by itself
prove how many were exact duplicates versus another store rejection class.
Requested k is therefore not the retained pool size.

At `k=128`, all four generators reported identical pool-size and overlap
summaries, although CPU and CUDA raw geometry fingerprints can still differ:

| Case | Reachable | Retained | Resource diversity | Geometric diversity |
| --- | ---: | ---: | ---: | ---: |
| Dense corridors | 128 | 3 | 0.719 | 0.683 |
| Sparse regions | 97 | 1 | n/a | n/a |
| Fragmented runs | 97 | 1 | n/a | n/a |
| High-turn maze | 97 | 1 | n/a | n/a |
| Cross-tile edges | 128 | 3 | 0.760 | 0.700 |
| Negative coordinates | 128 | 3 | 0.760 | 0.700 |
| Disconnected fields | 0 | 0 | n/a | n/a |
| KiCad fixture | 128 | 2 | 1.000 | 1.000 |
| Symmetric dual corridor | 128 | 2 | 1.000 | 1.000 |
| Multi-channel bottleneck | 118 | 4 | 0.558 | 0.417 |
| Policy alternatives | 123 | 4 | 0.616 | 0.500 |

The measured complete alternative-policy schedule produced resource-distinct
retained candidates in the symmetric, KiCad, bottleneck, dense, cross-tile,
negative, and policy-specific cases. It produced only one retained candidate
in the sparse, fragmented, and high-turn cases; whether other useful corridors
exist remains unmeasured. The implementation records zero diversity when fewer
than two candidates exist; the table reports that state as not applicable
rather than interpreting the zero as measured separation. In seven
intermediate case/k pools, GPU tie-breaking produced one additional equal-cost
geometry compared with CPU; by `k=128`, pool size and diversity summaries
agreed across all generators.

Sequential and parallel CPU produced identical visible execution outcomes and
geometry fingerprints for all 66 case/k prefixes, and frontier and sweep did
the same for all 66. Full stored-candidate order checksums still encode
generator identity and provenance and are not asserted byte-identical across
generator kinds. CPU and CUDA geometry differed in the 18 dense, cross-tile,
and negative-coordinate prefixes while scalar cost still matched. The two
CUDA algorithms therefore add no measured complementary candidate diversity
in this corpus; their bakeoff is a performance comparison.

The intrinsic best-of-k curve is flat for every reachable result. Candidate
zero is already CPU-optimal for the intrinsic compiled cost, so later
ban/penalty/objective variants can preserve or worsen that metric but cannot
improve it. Phase 3 demonstrates resource diversity, not an intrinsic-cost
improvement. A richer future quality curve must name a metric that values the
new alternatives rather than reuse the base path's already-optimal objective.

## Decision and remaining scope

Measured fact: shared prepared uploads and GPU-heavy batched exploration are
not enough to beat the best CPU mode end to end on this corpus through
`k=128`. APGAR keeps CPU A* as production dispatch, oracle, and fallback. No
automatic GPU crossover is introduced.

Future hypotheses, not Phase 3 facts, include larger compiled fields, batches
beyond 128, persistent kernels, lower-readback reconstruction, and alternative
policies with higher distinct-candidate yield. They require another
exact-commit bakeoff before any dispatch change.

This report does not cover exact through-via routing or via-template
compilation, allocator prices/worlds or selection, portals, legalization, CAD
transactions, multipin nets, or specialty routing. It does not claim M1 is
complete.
