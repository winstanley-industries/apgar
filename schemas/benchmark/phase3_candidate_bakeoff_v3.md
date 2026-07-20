# Phase 3 Persistent Compact Sweep Result Contract v3

V3 is the follow-up evidence contract for ADR-014. It retains v2's Google
Benchmark 1.9.5 format, clean VCS workspace stamp, corpus and policy schedule,
semantic outcome checksums, counter/accounting identities, CUDA provenance,
and hermetic toolchain requirements except where this document explicitly
changes the matrix or timing scopes. It does not reinterpret or replace the
committed v2 artifact.

The canonical v3 matrix contains the eleven versioned Phase 3 cases, candidate
counts `4,8,16,32,64,128,256,512`, and sequential CPU A*, parallel CPU A*, and
batched CUDA sweep. It contains exactly these three timed stages:

- `execution_readback`: generation against one prepared view; CUDA includes
  execution, compact readback, hostile-output reconstruction, and exact route
  validation, but excludes candidate admission and store publication;
- `prepared_end_to_end`: after one untimed identical full-pipeline warm-up has
  filled the sweep cache and passed CPU differential and admission checks, the
  same prepared view is retained while generation, compact readback,
  hostile-output validation, exact candidate admission, a fresh deterministic
  store, quality metrics, and transient result release remain timed; and
- `end_to_end`: the complete cold path, including GPU flatten/upload and
  prepared-view/workspace release, plus the same admission and store work.

The prepared scope measures same-case, same-count steady-state throughput. Its
prepare/upload, explicit warm-up, and retained-view release are excluded. Its
fresh per-iteration Candidate Store is not persistent session state. The cold
scope deliberately cannot exercise workspace reuse because it creates and
releases its prepared view inside every timed iteration.

V3 retains twenty repetitions, 20 ms minimum sample time, 10 ms warm-up, and
the four Google Benchmark aggregates `mean`, `median`, `stddev`, and `cv`.
Aggregate rows are sufficient for conservative mean-difference uncertainty
qualification, but they do not provide raw repetition samples, paired trials,
median confidence intervals, or outlier inspection. Reports must distinguish
nominal median leads from statistically clear mean differences and publish
coefficients of variation for unstable decision-driving rows.

CUDA sweep uses eight-round host chunks, four kernels per round when runs are
present, three when none are present, four fixed launches for initialization,
query-major path construction, offset scan, and packed scatter, and at most
one unfinished-query finalization launch. Compact readback transfers result
headers, compact headers, and only the used packed state prefix. The prepared
view retains one bounded sweep workspace. Actual retained capacity, logical
batch bytes, persistent bytes, and their simultaneous peak remain distinct
validated counters.

Compact host validation uses `V(Q) = min(8, ceil(Q / 64))` deterministic
partitions for `Q > 0`, and zero for `Q = 0`. Each partition owns one
`ceil(S / 64)` visited-state bitset for represented-state count `S`; this
scratch is included in `batch_host_bytes`. Validation writes predetermined
query result slots, preserves canonical external query ordering, and falls
back to caller-thread execution if the host cannot create every worker.
Thread stacks are process/runtime overhead and are not covered by the logical
`maximum_host_bytes` payload formula, just as allocator bookkeeping and the
independent disconnected-result CPU oracle are excluded.

Prepared and cold full-pipeline rows use the production batch candidate
adapter. It builds one bounded sorted query-ID membership index and then
preserves every per-item immutable seal, query/policy attribution, batch,
generator, backend, device, route-association, and authenticated-CUDA check.
The index's eight-byte logical payload per validated batch item is included in
`peak_deterministic_host_bytes`; caller-owned request views and allocator
bookkeeping remain outside that logical-payload metric.
The legacy one-item adapter retains its standalone membership scan; canonical
full-pipeline evidence must not use that scalar path repeatedly.

Every median must remain ordered and deterministic and must independently
match the sequential CPU baseline's per-policy reachability/failure and scalar
cost through the semantic checksum and counter partitions. Disconnected CUDA
results still require CPU-oracle confirmation. Backend, validation, invariant,
resource, cancellation, unsupported, and invalid failures must remain zero for
the canonical corpus; `failed_queries` is allowed only as the exact sum of the
intentional unreachable/disconnected outcomes.

A v3 evidence validator must hash-bind the raw JSON, report, and ADR to the
exact implementation commit; require the complete 3 by 3 by 11 by 8 matrix and
all four aggregates; validate context, row names, counters, memory and launch
identities; recompute correctness totals; and recompute nominal and
uncertainty-qualified comparisons against the faster CPU mode. Workload-pooled
throughput and equal-row geometric means answer different questions and must
state their weighting explicitly.
