# Phase 3 Candidate Bakeoff Result Contract v1

The machine-readable Phase 3 result is pinned Google Benchmark 1.9.5 JSON with
`apgar_` context and counter keys. No repository-local timing or statistics
framework may replace Google Benchmark repetition, warm-up, iteration, real
time, or aggregate statistics.

Required context records result/corpus/candidate/policy/batch schema versions,
the exact commit as 40 lowercase hexadecimal characters,
Board/compiler/routing/rule identities, policy identities and seeds,
host/CPU/GPU/driver/backend metadata, the operator-recorded NVIDIA KMD version
from `nvidia-smi`, checksum-pinned LLVM/CUDA/GCC and Google Benchmark versions,
warm-up/repetitions, and the supported device class. The result distinguishes
the KMD version from CUDA's driver API compatibility value.

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
per batch, deterministic GPU-batch host bytes, CUDA-event milliseconds once per
batch, exact kernel/finalization launches, exact blocking status readbacks,
dispatched rounds, and the configured round chunk.
Rows also report reachability/failure, scalar cost, accepted/rejected counts,
peak deterministic owned host-payload bytes, process-lifetime peak RSS, unique
geometry/resource signatures, pairwise overlap/diversity, best-of-k curves,
and repeated-batch determinism. The RSS value is the monotonic `RUSAGE_SELF`
high-water mark for the entire benchmark process and is not attributable to an
individual row; it supplements rather than replaces deterministic subsystem
accounting. Upload, validation, admission, or failed-query costs must not be
hidden from end-to-end conclusions.

End-to-end rows include transient execution-result, candidate-store, and GPU
prepared-view teardown in the same timed iteration that creates them. The
separate prepared-upload row measures preparation, flattening, and upload only;
its prepared-view teardown is explicitly excluded.

The benchmark binary binds the source commit at compile time from the required
`--define=APGAR_COMMIT=<40 lowercase hex>` Bazel setting and rejects a runtime
`--apgar_commit` label that does not match. Shape-valid caller text alone is not
sufficient evidence of the built source revision.

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
are authoritative for launch and synchronization counts. `rounds_maximum` is a
query progress metric and must not be presented as a kernel-launch or blocking
readback count.

Dispatch may change from CPU only when reproducible end-to-end evidence,
quality, memory, determinism, and failure semantics support it. Kernel-only
throughput is insufficient.
