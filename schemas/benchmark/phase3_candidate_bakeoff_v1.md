# Phase 3 Candidate Bakeoff Result Contract v1

The machine-readable Phase 3 result is pinned Google Benchmark 1.9.5 JSON with
`apgar_` context and counter keys. No repository-local timing or statistics
framework may replace Google Benchmark repetition, warm-up, iteration, real
time, or aggregate statistics.

Required context records result/corpus/candidate/policy/batch schema versions,
exact commit, Board/compiler/routing/rule identities, policy identities and
seeds, host/CPU/GPU/driver/backend metadata, checksum-pinned LLVM/CUDA/GCC and
Google Benchmark versions, warm-up/repetitions, and the supported device class.

Rows compare sequential CPU A*, parallel host CPU A*, batched CUDA heuristic
frontier, and batched CUDA sweep for candidate pool sizes 4, 8, 16, 32, 64,
and 128 under identical prepared boards and ordered policies. Required corpus
families are dense corridors, sparse regions, fragmented runs, high-turn
mazes, cross-tile and negative coordinates, disconnected fields, symmetric
dual corridors, multi-channel/resource bottlenecks, penalty/ban alternatives,
and the KiCad fixture.

Preparation/upload, batch execution/readback, exact validation/admission, and
end-to-end time are separate measurements. Rows report latency and throughput,
candidates/sec, examined states, rounds, peak deterministic host bytes, peak
owned VRAM, reachability/failure, scalar cost, accepted/rejected counts, unique
geometry/resource signatures, pairwise overlap/diversity, best-of-k curves,
and repeated-batch determinism. Upload, validation, admission, or failed-query
costs must not be hidden from end-to-end conclusions.

Dispatch may change from CPU only when reproducible end-to-end evidence,
quality, memory, determinism, and failure semantics support it. Kernel-only
throughput is insufficient.

