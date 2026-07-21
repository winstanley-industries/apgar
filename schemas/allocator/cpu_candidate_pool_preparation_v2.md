# CPU Candidate-Pool Preparation v2

CPU Candidate-Pool Preparation v2 defines the initial authentic candidate
pools consumed by Phase 4 allocation sessions.

## Inputs and ownership

One synchronous invocation receives:

- schema version `2`; version `1` is rejected before worker dispatch;
- an exact Board Snapshot and authentic canonical `MultiNetWorkload` for that
  Board;
- one persistent preparer configured with an explicit worker count in
  `[1, 64]`;
- requested candidates per net, exactly `4`, `8`, or `16`;
- deterministic seed and positive step, bend, and resource-penalty increments;
- positive per-query CPU A* work, record, queue, and reconstruction bounds;
- positive complete-invocation net, query, aggregate route-work,
  concurrent-record, queue-entry, reconstruction-state, and policy-entry
  bounds, plus retained, per-draft, and aggregate generated-byte bounds; and
- a valid CandidateStore configuration capable of representing the complete
  empty source roster and worst-case generated/rejection item count.

The result is move-only and owns a fresh CandidateStore. It retains no Board or
workload reference. A preparer invocation is non-reentrant; a concurrent call
fails with `busy` and dispatches no work.

## Preflight

Before the first worker job, validate exact Board/workload/compiler/routing
associations and use widened arithmetic to check:

```text
requested columns = net count * requested candidates per net
aggregate route work = requested columns * maximum work units per query
concurrent records = persistent worker count * maximum records per query
concurrent queue entries = persistent worker count * maximum queue entries per query
concurrent reconstruction states = worker count * maximum reconstruction states per query
policy entries = sum(K * base entries + K - 1) over all nets
maximum draft bytes required = 4096 + 512 * reconstruction states
                               + 128 * maximum policy entries in one candidate
retained bytes = net count * CandidateStore bytes per net
generated working bytes = requested columns * maximum draft bytes
                          + net count * reconstruction states * 40
```

Requested columns are capped at `1,000,000`; aggregate route work is capped at
`1,000,000,000,000,000`; aggregate policy entries are capped at `100,000,000`.
The fixed `40`-byte edge-roster charge is the version-1 upper bound already
shared with device-policy normalization; it is independent of host ABI layout.
The producer-authenticated segment sequence is decoded once to obtain the exact
bounded edge count, then filled into one pre-sized roster whose capacity must
equal that count; geometric vector growth is not permitted.
Every successful builder output is checked against the configured per-draft
bound before its slot retains it. The full `CpuRoute` remains worker-local and
is destroyed after exact resource extraction and draft construction; only its
bounded canonical base edge roster crosses from wave one to wave two. The
concurrent reconstruction bound is the conservative pre-dispatch product of
the fixed worker count and per-query reconstruction cap; runtime resident-memory
measurement belongs to the frozen evidence runner.

The store's count dimension must accept at least `K` candidates per net, every
requested item as either a draft or rejection, every rejection in its retained
history, and one expected empty pool per net. Its independent per-net byte cap
may prune every otherwise admissible draft; those atomic admission rejections
remain explicit column outcomes and an empty pool is valid. Candidate
logical-byte and exact-admission work preflight occurs after generation but
before atomic publication.

## Deterministic execution

Canonical net order comes from the workload. Column order is net-major then
zero-based candidate ordinal. Executed queries use the one-based canonical
column index. Skipped columns use query identity `0` and policy identity `0`.
Every executed column records the exact CPU A* `work_units` returned by its
route or ordinary route-failure telemetry. An ordinary failure without route
telemetry records zero. Skipped columns always record zero. The aggregate
counter is the exact widened sum of ordered column work and must remain within
the accepted conservative aggregate preflight bound.

The base policy copies the workload request policy, replaces its candidate
ordinal with zero, and derives a per-net seed from the configured seed,
workload checksum, and net identity. Wave one runs this policy once per net.

The nonzero per-net seed uses the same `StableHashBuilder` encoding specified
below. It hashes domain `APGAR-CPU-CANDIDATE-POOL-NET-SEED-V1`, followed by the
configured seed `u64`, workload checksum `u64`, net ID `u64`, and net generation
`u32`; a zero final hash is remapped to `1`. The representation fixture
`(configured seed, workload checksum, net ID, generation) = (373, 379, 383,
389)` has golden value `4652715695970407240`.

For a reached base, authenticate the CPU producer evidence and expand only its
producer-authenticated exact segment sequence into canonical physical edges.
Sort and deduplicate that roster and use it with Alternative Policy v1 to
synthesize all `K` normalized policies. The redundant diagnostic
`CpuRoute::lattice_path` is not consumed. Policy zero must exactly replay the
base. Wave two runs columns `1..K-1`.

A base disconnected or unsupported result is a structured published rejection.
Its remaining columns are `skipped_after_disconnected_proof` or
`skipped_after_unsupported_proof`; no policy is synthesized and no route query
is run for them. Other route failures fail the invocation. Every reached route
passes the authenticated CPU candidate builder; a builder rejection remains an
ordinary published column outcome.

Workers write only their preallocated slot. Once all jobs finish, canonical
host order assembles one conditional CandidateStore invocation against an
explicit empty pool and exact association binding for every workload net.
Publication is all-or-nothing under CAN-004.

## Bounded CPU A* v1

The bounded overload accepts four positive `u64` maxima. One deterministic
work unit is charged immediately before each queue pop, each attempted
compiled-legal edge relaxation, and each reconstructed state. Record and queue
limits are checked before insertion; reconstruction is checked before append.
An equality-bound execution succeeds with the same authenticated route and
telemetry as the unbounded CPU oracle. Exhaustion returns
`kWorkBoundExceeded`, diagnostic telemetry no larger than the configured
container/work bound, and no producer evidence. Arithmetic inability to
represent a route cost remains `kResourceExhausted`. Pool preparation preserves
that distinction, fails atomically, and leaves its persistent workers reusable.

## Column records and counters

Every requested column records net, ordinal, policy, batch/query identity,
actual route work, outcome, and optional candidate ID, payload checksum, and
rejection code.
Outcomes are:

1. admitted;
2. duplicate;
3. route disconnected;
4. route unsupported;
5. skipped after disconnected proof;
6. skipped after unsupported proof;
7. candidate build rejected; or
8. exact/store admission rejected.

Counters record requested columns, executed queries, actual route work, successful routes,
disconnected and unsupported proofs, skipped columns, built candidates,
admitted candidates, duplicates, rejected columns, and retained candidates.
Explicit empty pools are returned for nets with no admitted candidate.

## Semantic identities

Both identities use the Board IR v1 `StableHashBuilder`: FNV-1a with offset
basis `14695981039346656037` and prime `1099511628211`. Unsigned integers are
encoded least-significant byte first at their named width; booleans are one
byte `0` or `1`; enums are one byte; and strings are a little-endian `u64`
byte count followed by the bytes without a terminator.

The complete semantic configuration encoding is, in order:

1. schema and requested candidates as two `u32`;
2. seed, step increment, bend increment, and resource-penalty increment as four
   `u64`;
3. route work, record, queue, and reconstruction maxima as four `u64`;
4. net, query, aggregate route-work, concurrent record, concurrent queue,
   concurrent reconstruction, aggregate policy-entry, retained-byte,
   per-draft-byte, and aggregate generated-byte maxima as ten `u64`; and
5. CandidateStore candidate count/bytes, rejection record/item, admission
   item/bytes/work, pin item, expected pool, and expected candidate maxima as
   ten `u64`.

The nonzero batch identity hashes the domain
`APGAR-CPU-CANDIDATE-POOL-BATCH-V2`, Board hash, workload checksum, and the
complete semantic preparation configuration, including CandidateStore limits.
The domain uses the string encoding above. Board and workload hashes are two
`u64` before the configuration. A zero final hash is remapped to `1`. The
representation fixture uses Board/workload `(127, 131)` and the prime-valued
configuration in the compatibility test; its batch identity is
`9699625364364086907`. Persistent worker count and operational telemetry are
excluded.

The replay checksum hashes the domain
`APGAR-CPU-CANDIDATE-POOL-PREPARATION-V2`, the same configuration, batch
identity, all counters, every ordered column field with presence tags, and each
ordered retained pool's candidate IDs and payload checksums. Worker count,
completion order, elapsed time, pointer identity, and runtime thread identity
are excluded.

After the domain and configuration, encode:

1. batch identity as `u64`;
2. the twelve counters in declaration order as `u64`;
3. column count as `u64`, then each column as net ID `u64`, generation `u32`,
   ordinal `u32`, policy/batch/query identities and actual route work as four
   `u64`, outcome `u8`,
   candidate-ID presence `u8` and optional high/low `u64`, payload presence
   `u8` and optional `u64`, rejection presence `u8` and optional code `u8`;
4. pool count as `u64`, then each pool as net ID `u64`, generation `u32`,
   candidate count `u64`, and every candidate ID high/low plus payload checksum
   as three `u64`.

Outcome tags `0..7` follow the listed outcome order above. Candidate rejection
tags are defined by `schemas/candidate/rejection_v1.md`. The two-column/two-pool prime-valued
representation fixture in the compatibility test has checksum
`3669270561708826273` when its batch identity is the golden above.

## Failed preparation observation

A fatal error after at least one CPU query starts returns a bounded diagnostic
observation on the preparation error. It contains Board and workload identity,
the complete configuration and batch identity, requested columns, attempted
queries, available actual route work, and every actually attempted column in
canonical order. An attempt records net, ordinal, scheduling identities, work,
whether the route call is in flight, succeeded, or failed, and the optional
route-failure code. A returned route failure uses its telemetry; absent
telemetry records zero. Every dispatched wave joins before the observation is
assembled, and a fatal base wave never dispatches alternatives.

Domain `APGAR-CPU-CANDIDATE-POOL-FAILED-PREPARATION-V2` hashes schema, Board and
workload identities, complete configuration, batch identity, parent error code,
the CandidateStore-publication-committed boolean, the three counters, and every
attempted-column field with a presence tag for the route-failure code. The
prime-valued representation fixture with an uncommitted store hashes to
`3718100591794585442`.

Every host, policy, worker, staging, publication, correlation, or assembly
failure after the first query starts carries this observation. Before atomic
publication, the commit bit is false and the observation owns no store. Once
the atomic publication succeeds, the commit bit becomes true; any later error
transfers the authoritative committed CandidateStore into the move-only
observation rather than destroying it or implying rollback. Store pointer
identity and contents are not duplicated into the checksum: the checked commit
bit declares whether the owned store is required, while CandidateStore's own
typed associations and candidate evidence remain authoritative. A preflight or
pre-query error has no failed observation.

## Deliberate boundary

This schema establishes bounded persistent CPU preparation and exact initial
pools. It does not define sequential negotiation, multi-world allocation,
equal-budget timing, GPU execution, combined-route legalization, or a Phase 4
completion decision.
