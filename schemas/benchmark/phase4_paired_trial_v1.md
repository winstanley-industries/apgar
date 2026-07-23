# Phase 4 Paired Equal-Budget Trial v1

Phase 4 Paired Trial v1 is the decision-eligible semantic boundary between the
Sequential Negotiated-Routing Baseline v1 and the reusable CPU Candidate-
Allocation Session v3. It executes one contender in one independently built
representative case, destroys heavyweight state, and returns a compact result.
An external isolated-process authority then associates wall-clock and peak-host
memory observations before two arms may be paired.

This contract is not the corpus report or its statistical validator. It emits
one authenticated successful arm and one authenticated successful pair. Failed,
timed-out, or memory-exhausted attempts remain evidence at the outer harness
layer and are never converted into algorithm losses.

## Input identity

One `Phase4PairedTrialSpec` contains schema version `1`, representative case
and pool size, repetition index, one root seed, prescribed AB or BA order,
candidate preparation worker count, corpus build limits, complete baseline,
preparation, and candidate-session configurations, and four external caps:
prepared elapsed nanoseconds, cold elapsed nanoseconds, virtual address-space
bytes, and peak resident host bytes.

The paired wire remains schema v1 when a nested allocator schema changes:
`budget_checksum` authenticates the complete baseline, preparation, and
candidate-session configurations, including the candidate-session schema
version. Statistical Protocol v4 additionally freezes the complete per-cell
canonical algorithm-budget checksum roster before observations. A nested
schema change therefore requires a new frozen budget roster and protocol
authority, but not a paired-wire version when no paired field, order, width, or
decision rule changes.

Pool size must be both declared by the case descriptor and one of `4`, `8`, or
`16`. The fixed-query query-shape descriptors requesting `1` or `1024` are
diagnostic corpus cases; Candidate-Pool Preparation v2 cannot execute those
sizes, and this runner rejects rather than clamps them.

The root seed must equal all three contender roots:

```text
trial root = sequential deterministic seed
           = initial preparation deterministic seed
           = targeted execution deterministic seed
```

Baseline, preparation, and targeted execution use identical values for all
four per-query `CpuRouteWorkLimits`. Baseline and candidate session also use
identical negotiated-price configuration, intrinsic-cost weight, and One-World
allocator limits. The top-level exact-conflict count must match the corpus;
session-owned nested targeted and Multi-World counts remain zero.

## Equal opportunity and stopping lineage

Let:

```text
N = descriptor requested net count
S = baseline maximum sweeps
K = requested initial candidates per net
E = candidate maximum regeneration epochs
C = targeted-plan maximum total columns per epoch
R = maximum selection rounds of any terminal Multi-World schedule
W = maximum CPU A-star work units per query
```

The canonical Isolated Raw Evidence v1 harness fixes `W` at
`1,000,000,000`. This is an explicit finite opportunity cap, not the unbounded
default of the standalone CPU A-star API.

`C` is accepted only when it is structurally reachable through the other
planner bounds. With maximum initial roster `N*K`, One-World candidate
headroom `H`, maximum retained target count `T`, and per-target column ceiling
`L`, widened validation requires:

```text
H = allocator maximum candidates - N*K
T = min(N, maximum target nets, maximum total resource actions, H)
L = min(maximum columns per net, maximum resource actions per net + 1)
C <= H
C <= T * L
C <= maximum total resource actions + T
```

Zero per-net resource actions makes `L=0`. A structurally unreachable declared
`C` is rejected rather than credited as candidate opportunity.

All products and sums are checked in widened unsigned arithmetic. A trial is
admissible only when:

```text
baseline query opportunity  = N * S
candidate query opportunity = N * K + E * C
N * S = N * K + E * C
route-work opportunity = query opportunity * W
S = E + R - 1
```

The baseline route-query/work caps equal `N*S` and `N*S*W`; preparation caps
equal `N*K` and `N*K*W`; session aggregate requested-column, query, and work
caps equal `E*C`, `E*C`, and `E*C*W`; and one targeted execution is capped at
`C` and `C*W`. Equality is deliberate: the canonical runner exposes no hidden
extra route opportunity. Actual completed queries and CPU A-star work may be
lower and need not match across contenders, but each must not exceed the shared
opportunity.

## Canonical corpus execution envelope

The canonical harness derives route and retained-state bounds from the frozen
representative geometry instead of inheriting generic allocator defaults. Let
`M` be the largest pool declared by the case descriptor, including when the
active cell uses a smaller pool. The v1 corpus proves every supported route
fits within `32*M` reconstruction states: portal and fragmented witnesses use
at most `24*M+14`, while pin-field and imported witnesses are smaller. Define:

```text
X = 32*M
B = N*X
P = 2*B+N
```

`B` bounds one selected candidate footprint per net. `P` conservatively binds
the price roster, occupancy roster, and at most one capacity override per net.
The harness sets the shared price-record and baseline occupancy caps to `B`.
For every executable success cell, `P` fits the one-million-record One-World
limit.

Candidate byte bounds use the existing exact-admission projection constants:

```text
draft(states, policy entries) = 4096 + 512*states + 128*policy entries

baseline draft     = draft(X, P)
preparation draft  = draft(X, 1)
regeneration draft = draft(X, B)
```

Baseline retained bytes are `N*baseline draft`. Preparation generated bytes
are `N*K*preparation draft + N*X*40`. One regeneration epoch generates at
most `N*regeneration draft`; session totals multiply that, policy projection,
rejection, and transient-result bounds by `E`. The shared CandidateStore
transaction input bound is the maximum of the initial and one-epoch
regeneration projections, including the stable `57` fixed-policy bytes and
`29` bytes per policy entry. Aggregate regeneration policy entries are capped
at the execution schema's canonical 250-million-entry ceiling; every one-net
policy still fits its one-million-entry schema limit.

All formulas use widened arithmetic and are independently checksum-bound in
the representative manifest. Stress descriptors `3001` and `3002` remain
declared corpus-construction work-bound rows: they retain a deterministic
configuration checksum but are not executable-success promises.

## Arm execution

Each call independently builds exactly one Representative Corpus v1 case.
Case descriptor, net count, Board/workload association, corpus checksum,
descriptor fingerprint, case checksum, Board content hash, workload checksum,
and capacity-model checksum become paired identity.

The baseline arm executes the named sequential reference and retains its final
One-World board outcome. The candidate arm prepares the initial pools through
the caller-owned matching persistent preparer and executes Candidate-Allocation
Session v3. Its actual queries and work are widened sums of preparation and
regeneration counters. Its decision outcome is the preferred retained
Multi-World when one exists; otherwise it is the common-lineage terminal
One-World. A preferred identity missing from the retained roster is an internal
invariant failure.

Both arms normalize terminal reasons to feasible, no admissible candidate,
fixed-point stalled, stopping budget exhausted, or resource refinement
required. They publish requested/admitted/rejected columns, final candidate
count, component replay checksums, final manifests, exact actual CPU route
work, and this board outcome:

```text
(selected net count,
 no-candidate net count,
 overused resource count,
 total overuse units,
 total unweighted intrinsic cost,
 world checksum)
```

Authenticated counters must still describe a possible execution. Opportunity
work is an exact positive per-query bound. Actual and candidate-component work
may not exceed their query counts times that bound, actual queries may not
exceed requested columns, admitted plus rejected columns must exactly partition
requested columns, and selected nets must fit within final candidates, which
must fit within admitted candidates. Overused-resource count may not exceed
total overuse units.

`prepared_elapsed_nanoseconds` covers the complete selected algorithm after
case construction. `cold_elapsed_nanoseconds` starts before case construction
and stops only after algorithm result, case, candidate pools, worlds, and other
heavyweight state have been destroyed. Candidate worker-pool construction is
outside both scopes because the production preparer must persist across
candidate repetitions.

## External finalization

An arm can be finalized only when an External Authority v1 observation names
its exact semantic checksum and checksum-binds a nonzero controller run,
controller, and process-instance identity; the exact configured wall,
address-space, and peak-resident limits; process exit status; outer elapsed
time; Linux `wait4` process-lifetime peak resident use;
and candidate-preparer telemetry before and after the invocation. The arm
worker captures those counters around the measured contender call; the
external observation must repeat the captured values exactly. It also
asserts all of:

- a distinct isolated process;
- enforced wall-time authority;
- enforced address-space and peak-resident-memory authority;
- prepared inner time, cold inner time, authoritative outer time, and peak
  bytes within their declared caps; and
- outer time not shorter than inner cold time.

The disjoint case-build and prepared intervals must widened-sum to no more than
the cold interval. `RLIMIT_AS` is an exact, conservative virtual-address-space
safety cap; it is not relabelled as an RSS limiter. The separately declared
peak-host cap is checked against widened Linux `ru_maxrss * 1024`. The
authority checksum domain is
`APGAR-PHASE4-EXTERNAL-AUTHORITY-V1`. V1 names the Linux parent-watchdog,
`RLIMIT_AS`, and `wait4` authority kind; a zero identity, nonzero exit, changed
limit, zero peak observation, or checksum drift is rejected.

After the domain, encode authority schema `u32`, kind `u8`, run/controller/
process/semantic identities as four `u64`, configured wall, address-space, and
peak-host limits, outer elapsed time, and observed peak bytes as five `u64`,
exit code `i32`, the four
isolation/authority/reuse booleans as `u8`, and the six preparer lifecycle
counters as `u64`, all in declaration order. The checksum field itself is
excluded.

`persistent_preparer_reused` must be false for the sequential arm and true for
the candidate arm. Baseline worker-captured and authority lifecycle counters
are all zero. Candidate telemetry must show the declared workers already
started, at least one prior completed invocation, an idle preparer before
measurement, and exactly one additional started and completed invocation
without restarting workers. The declared worker count must be within the
supported persistent-preparer range. The process-controller contract keeps one
separately exec'd worker per contender alive across every repetition in one
case/pool/worker cell. Both contenders complete one declared untimed warm-up;
the candidate retains the same preparer and its workers. All repetitions from
that process share its conservative process-lifetime `wait4` peak; a later
abnormal process exit invalidates every otherwise successful arm from that
process.
Finalization does not infer authority from a timer or self-reported allocation
counter inside the worker.

## Pairing and decision

Pairing accepts exactly one finalized sequential arm and one finalized
candidate arm. Their schema, prescribed order, corpus/case/Board/workload/
capacity identities, budget checksum, net/pool/repetition/root, operational
worker count, stopping lineage, external caps, and route opportunity must be
identical. Their actual work is intentionally allowed to differ.
Both observations must come from one controller run and authority kind but
name distinct process-instance identities. Assembly reruns every semantic,
authority, limit, lifecycle, timing, and artifact check; recomputing the public
unkeyed checksums cannot launder an invalid finalized arm.

The comparison is lexicographic:

1. maximize selected net count;
2. minimize total overuse units; then
3. minimize total unweighted intrinsic cost.

Other outcome fields remain authenticated diagnostics and tie witnesses. A
pair compares only two successful arms; the outer evidence validator decides
how failed attempts affect completeness and statistics.

## Stable checksums

All identities use Board IR v1 `StableHashBuilder`. Unsigned integers are
little-endian at their declared width, booleans and enum tags are one byte, and
strings are a `u64` byte count followed by bytes.

`APGAR-PHASE4-CANONICAL-ALGORITHM-BUDGET-V1` hashes every field of all three
algorithm configurations in declaration order, with schedules in canonical
`schedule_key` order. `APGAR-PHASE4-PAIRED-BUDGET-V1` hashes
schema/corpus/case/pool/root, corpus limits, derived opportunity and stopping
quantities, that canonical algorithm checksum, and all four external caps in
declaration order. The frozen representative manifest publishes the expected
algorithm checksum for every case/pool cell, allowing the independent raw
validator to reconstruct the complete budget checksum. Caller schedule order
is nonsemantic.
Repetition, prescribed order, and worker count are outside the budget.

`APGAR-PHASE4-TRIAL-ARM-SEMANTIC-V1` hashes arm role, corpus and built-case
identity, capacity and budget checksums, workload/pool/repetition/root,
normalized stopping quantities, external caps, opportunity, actual and
partitioned work/counters, component/manifests checksums, normalized terminal
reason, outcome source, and board outcome in declaration order. It excludes
its own checksum, execution order, worker count, and all timing/resource
observations.

`APGAR-PHASE4-TRIAL-ARM-ARTIFACT-V1` hashes the semantic checksum, prescribed
execution order, worker count, three inner timings, the six worker-captured
preparer lifecycle counters, and the authenticated External Authority v1
checksum. `APGAR-PHASE4-PAIRED-TRIAL-SEMANTIC-V1` hashes schema, both arm
semantic checksums in baseline/candidate order, and comparison.
`APGAR-PHASE4-PAIRED-TRIAL-ARTIFACT-V1` hashes the pair semantic checksum,
prescribed order, and both arm artifact checksums.

## Failed arms and authoritative state

An execution failure is a move-only `Phase4TrialArmFailure`. Its small summary
is indexing metadata only and cannot replace a required typed payload. A
case-build failure owns the complete typed corpus
error. A sequential failure owns the independently built case and typed
baseline error. A preparation failure owns the case and complete move-only
preparation error, including any attempted-query observation and authoritative
post-publication CandidateStore. A session failure owns the case, caller-owned
prepared pools/store, and complete typed session error. The child invariant,
bound witnesses, attempted work, epoch, failed regeneration, and publication-
committed bit are never flattened into the summary. The outer artifact layer
must retain or explicitly reconcile this payload before discarding committed
state.

Any reconciled failed-query observation has one attempted-column record per
attempted route query, and its aggregate work cannot exceed the query count
times the canonical Phase 4 per-query work limit. Process-setup failures that
occur before a case-bearing typed payload exists use only summary codes valid
for a worker at that boundary; persistent-preparer factory resource exhaustion
is `kResourceExhausted`, not a fabricated candidate-preparation payload.
