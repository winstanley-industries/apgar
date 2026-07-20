# Phase 3 Candidate Bakeoff Result Contract v2

The machine-readable Phase 3 result is pinned Google Benchmark 1.9.5 JSON with
`apgar_` context and counter keys. No repository-local timing or statistics
framework may replace Google Benchmark repetition, warm-up, iteration, real
time, or aggregate statistics.
The Google-produced `context.library_version` and the APGAR-provided
`apgar_google_benchmark_version` must both be the string `1.9.5`;
`json_schema_version` must be the integer `1` and JSON booleans are not
compatible integer values.

Required context records result/corpus/candidate/policy/batch schema versions,
the exact commit as 40 lowercase hexadecimal characters,
Board/compiler/routing/rule identities, policy identities and seeds,
the candidate-store rejection-ingestion cap and three admission-transaction
caps,
host/CPU/GPU/driver/backend metadata, the operator-recorded NVIDIA KMD version
from `nvidia-smi`, checksum-pinned LLVM/CUDA/GCC and Google Benchmark versions,
warm-up/repetitions, and the supported device class. The result distinguishes
the KMD version from CUDA's driver API compatibility value.

The producer derives candidate-count and store-cap context values from its
compiled C++ arrays/constants. The evidence validator independently pins the
v2 values stated by this schema; it does not trust an artifact to choose its
own matrix, resource bounds, or launch model. Thus these intentional
cross-language checks are compatibility validation, not a second producer
configuration. Canonical v2 uses requested counts `4,8,16,32,64,128`, a
benchmark rejection-ingestion cap of `128`, and the Candidate Store v1 default
admission caps of `1024` items, `67108864` input bytes, and `100000000` work
units. It also records machine-checkable launch parameters: frontier/sweep
chunks of `32`/`8` rounds, one frontier launch per chunk, three/four sweep
kernels per round without/with compiled runs, two fixed batch launches, and at
most one finalization launch. Human-readable launch-model strings are
explanatory only.

Rows compare sequential CPU A*, parallel host CPU A*, batched CUDA heuristic
frontier, and batched CUDA sweep for requested ordered-policy prefixes of 4, 8,
16, 32, 64, and 128 under identical prepared boards. Each row reports the
requested count and the actual retained candidate-pool size separately because
unreachable policies and deterministic deduplication need not produce one
stored candidate per query. Required corpus families are dense corridors,
sparse regions, fragmented runs, high-turn mazes, cross-tile and negative
coordinates, disconnected fields, symmetric dual corridors,
multi-channel/resource bottlenecks, penalty/ban alternatives, and the KiCad
fixture.

Preparation/upload, batch execution/readback, exact validation/admission, and
end-to-end time are separate measurements. Rows report latency and throughput,
candidate queries/sec, generated routes/sec, accepted candidates/sec,
query-local examined states and rounds, persistent/batch/peak owned VRAM once
per batch, persistent prepared node-lookup host bytes, deterministic transient
GPU-batch host bytes, CUDA-event milliseconds once per batch, exact
kernel/finalization launches, exact blocking status readbacks, dispatched
rounds, and the configured round chunk.
Rows also report reachability/failure, scalar cost, accepted/rejected counts,
builder rejections, store rejections, total retained rejection records,
peak deterministic owned host-payload bytes, process-lifetime peak RSS, unique
geometry/resource signatures, pairwise overlap/diversity, best-of-k curves,
and repeated-batch determinism. The RSS value is the monotonic `RUSAGE_SELF`
high-water mark for the entire benchmark process and is not attributable to an
individual row; it supplements rather than replaces deterministic subsystem
accounting. Upload, validation, admission, or failed-query costs must not be
hidden from end-to-end conclusions.

Every generator/stage run must publish every counter required for that stage on
all four Google Benchmark aggregates. Median rows additionally satisfy exact
accounting identities: reached plus failed equals requested; the complete
failure-class partition equals failed; accepted plus rejected equals reached;
builder plus store rejections equals rejected; retained rejection records equal
rejected; retained pool size equals accepted; and peak owned VRAM equals
persistent plus batch-owned VRAM. Accepted candidate yield and
generated-candidate acceptance are derived from those same counts.
Geometry/resource signature counts equal the deduplicated retained pool size,
and overlap/diversity values remain in `[0, 1]` with diversity equal to one
minus mean overlap for pools of at least two candidates.

V2 requires `semantic_outcome_checksum_hi` and
`semantic_outcome_checksum_lo` on every generator/stage aggregate. Together
they encode the stable 64-bit checksum with domain
`APGAR-PHASE3-BENCHMARK-SEMANTIC-OUTCOME-V1` over orderedness, query count, and
each ordered query's input ordinal, query identity, policy identity, failure
class, and scalar cost; geometry is intentionally excluded. The validator does
not accept `differential_match=1` as sufficient evidence. For every case and
requested policy prefix it independently compares the complete failure-count
partition, base/minimum/summed reachable scalar costs, and the semantic
checksum of every generator/stage median to sequential CPU A*.

End-to-end rows include transient execution-result, candidate-store, and GPU
prepared-view teardown in the same timed iteration that creates them. The
separate prepared-upload row measures preparation, flattening, and upload only;
its prepared-view teardown is explicitly excluded.

The benchmark binary binds the source commit and dirty state at build time from
Bazel stable workspace status produced by `tools/workspace_status.sh` under
`--config=benchmark`. It rejects unstamped or dirty builds and a runtime
`--apgar_commit` label that does not match the VCS-derived 40-character commit.
Caller-controlled build definitions and shape-valid runtime text are not source
identity evidence. Published JSON records
`apgar_source_identity=bazel_stable_workspace_status_v1` and
`apgar_source_tree_dirty=false` alongside the commit.

`examined_states` uses a generator-specific, deterministic state unit: CPU A*
reports expanded states, CUDA frontier reports closed winner states (excluding
the terminal empty-open-set round of a disconnected query), and CUDA sweep
reports node-heading departure evaluations (`represented_nodes * 8 * rounds`).
`examined_work_items` retains CPU expanded-state, frontier legal-edge
relaxation-attempt, and sweep run-edge propagation-attempt counts. These values
explain scaling inside one generator; their raw magnitudes are not comparable
cross-generator work scores.

CUDA frontier rows identify 32-round cooperative A* chunks; CUDA sweep rows
identify 8-round departure/run-parallel chunks. The batch telemetry counters
are authoritative evidence for launch and synchronization counts, while the
validator independently checks `chunk_rounds` against these schema-pinned
algorithm constants. A producer cannot redefine that contract by emitting a
different numeric counter. `rounds_maximum` is a query progress metric and must
not be presented as a kernel-launch or blocking readback count.
CPU rows report zero for GPU-owned memory, CUDA timing, launches, readbacks,
dispatch rounds, finalization, prepared node-lookup ownership, and chunk
counters. GPU rows report prepared-view persistent device and node-lookup host
bytes consistently, peak device bytes as persistent plus batch bytes, and one
blocking status readback per dispatched chunk. Frontier launches are the
two fixed initialization/predecessor launches plus one launch per chunk and an
optional finalization launch. Sweep launches are the two fixed launches plus
three kernels per dispatched round when the compiled board has no runs or four
when it has runs, plus the optional finalization launch.

Compared with v1, v2 requires VCS-derived clean-tree source identity, explicit
candidate-store admission transaction caps, retained rejection accounting, and
the geometry-independent ordered semantic outcome checksum. Those fields are
mandatory and must not be inferred when validating a v1 artifact.

Dispatch may change from CPU only when reproducible end-to-end evidence,
quality, memory, determinism, and failure semantics support it. Kernel-only
throughput is insufficient.
