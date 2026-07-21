# Phase 4 Isolated Raw Evidence v1

Phase 4 Isolated Raw Evidence v1 is the fail-closed operating-system harness
around `phase4_paired_trial_v1`. It records one case/pool/worker cell, including
every successful, failed, timed-out, or unstarted attempt. Only a complete cell
of decision-eligible paired results may enter Phase 4 statistics.

## Canonical cell

A cell fixes one Representative Corpus v1 case, one declared pool size from
`4`, `8`, or `16`, one preparation-worker count, corpus build limits, setup
deadline, and the four Paired Trial v1 external caps. Publication uses four
persistent preparation workers and exactly 20 repetitions. Repetition `i`
uses baseline-first order when `i` is even and candidate-first order when it
is odd, yielding ten observations of each order.

`phase4_representative_manifest_v1.json` freezes the exact decision-eligible
case IDs, pool rosters, net counts, descriptor fingerprints, built case,
Board IR, workload, and capacity-model checksums. It also records the 2,048-
and 4,096-net stress descriptors as deterministic compiled-work-bound results;
those entries cannot be presented as successful raw cells. The strict
validator binds every successful arm to this manifest and its corpus checksum.
It independently rejects a successful cell whose configured maximum nets,
compiled nodes, compiled host bytes, active regions, or Board entities are below
the manifest's frozen requirements, even when all serialized checksums agree.
For every declared case/pool, the manifest also pins the canonical algorithm-
configuration checksum. The validator combines it with the serialized corpus
limits, derived `N/K/E/C/R/S/W` quantities, root seed, and external caps to
independently reconstruct `budget_checksum`; it also enforces the separate
`N*K` preparation and `E*C` regeneration query/work caps.

Setup and cold watchdog caps are finite, nonzero, and at most 24 hours;
prepared time cannot exceed cold time. The address-space cap must be exactly
representable by `rlim_t` and must not equal `RLIM_INFINITY`.

The canonical spec sets:

```text
E = 2
C = N
R = K + 1
S = K + 2
W = 1,000,000,000 CPU A-star work units per query
```

Therefore `N*S == N*K + E*C` and `S == E + R - 1` by construction. One root
seed is derived only from the frozen corpus checksum, case ID, and pool size;
it does not vary with repetition, order, process identity, or worker count.
The cell-plan checksum domain is
`APGAR-PHASE4-CANONICAL-CELL-PLAN-V1` and hashes the harness schema, corpus
checksum, cell fields, all four external caps, and corpus limits in declaration
order.

## Isolated controller

The controller launches two separately exec'd instances of the exact evidence
runner binary, one per contender. It uses dedicated close-on-exec request and
response socket channels plus a bounded merged stdout/stderr channel and a
pre-exec error pipe; never invokes a shell; gives each worker its own process
group; installs a parent-death signal; and applies
the exact `RLIMIT_AS` safety cap before `exec`. The worker verifies that limit
before reporting ready. Before launch, the controller rejects every inherited
`SIGCHLD` policy except the default handler without `SA_NOCLDWAIT`, verifies
Linux pidfd support, and acquires one pidfd for each child. The retained pidfd
pins the leader identity used for signals and non-consuming exit observation.
Before `exec`, the child closes every unrelated inherited descriptor with
Linux `close_range`, falling back deterministically through the captured
`RLIMIT_NOFILE` bound when that syscall is unavailable. Only request descriptor
3, response descriptor 4, merged output descriptors 1 and 2, and the
close-on-exec launch-error pipe survive until `exec`.

Worker setup and warm-up are serial: the baseline worker completes one untimed
warm-up before the candidate worker is launched, and the candidate then creates
one persistent preparer and completes one untimed warm-up. Measured contender
invocations are released serially in the prescribed AB/BA order and are timed
by an absolute `CLOCK_MONOTONIC` parent watchdog. Setup timing starts before
worker launch, and measured timing starts before command dispatch. Diagnostic
drains are byte-bounded per pass and recheck the same absolute deadline so a
worker cannot postpone a watchdog by continuously producing output. The
candidate keeps the same preparer and worker threads for every repetition in
the cell.

Each `Ready` frame is sent only after its warm-up succeeds and carries the
warm-up semantics' case ID, descriptor fingerprint, case checksum, Board hash,
workload checksum, and capacity checksum. The parent authenticates and retains
both Ready identities, checks the case ID and descriptor against the active
cell, requires the two full identities to be identical, and associates every
later success or identity-bearing failure with that worker's retained Ready.
Before a worker reaches Ready, a summary-only setup failure carries no case
identity, a corpus failure must name the active case ID, and every other typed
failure must name the active case ID and descriptor. If the peer is already
Ready, the failure's complete built-case identity must also match that retained
peer identity.

The controller sends only fixed-width little-endian Wire v1 frames. An envelope
contains the eight-byte magic, `u32` schema, `u8` kind, bounded `u32` payload
length, payload, and `u64` FNV-1a checksum. Payloads are limited to 65,536
bytes. Decoding rejects unknown schemas, kinds, and enums, noncanonical
booleans, oversized lengths before allocation, truncation, trailing bytes, and
checksum drift. A successful stop requires the worker's explicit `Stopped`
frame followed by response-channel EOF with no intervening byte; the frame
alone is only a provisional acknowledgement. C++ object layouts, pointers,
`string_view`, and owning stores never cross the process boundary. Any worker
stdout or stderr during a nominally successful cell invalidates that worker's
successes.

After all commands, the parent stops and reaps each exact PID with nonblocking
`wait4` under the teardown deadline. Until the response channel reaches EOF or
a terminal protocol failure is resolved, pidfd `waitid` with `WNOWAIT` observes
liveness without releasing the original child PID. A timeout kills the process
group only while its leader is provably unreaped and signals a running leader
through its pidfd; post-kill reaping has its own bounded grace and never falls
back to an unbounded blocking wait. Linux `ru_maxrss` is checked and converted
from KiB to bytes. Every arm from one worker shares that process-lifetime peak.
A later abnormal exit, fatal protocol or association failure, trailing
response, missing EOF, or unavailable exact reap invalidates all earlier
apparent successes from the same worker, including when the worker later exits
cleanly. `RLIMIT_AS` remains a virtual-address-space safety cap and is never
described as peak RSS.

## Total attempt state

Every arm attempt has exactly one disposition:

```text
0 success
1 typed child failure
2 setup timeout
3 measured wall timeout
4 signal termination
5 nonzero exit
6 protocol failure
7 launch or exec failure
8 external budget exceeded
9 teardown timeout
10 not run after a fatal cell event
```

Only disposition `0` may contain a finalized arm record, and only two such
records may produce a paired result. Watchdog, signal, process, launch,
protocol, or budget failures are incomplete evidence, never algorithm losses.
After the first fatal event, no later measured arm is released. A successful
cell also requires an authenticated stop acknowledgement followed by an exact
clean `wait4` reap from both workers; teardown drift invalidates every earlier
success from the affected process.

A typed child failure retains its paired summary plus a stable payload kind,
child error and bound witnesses, optional case and epoch identity, failed
observation counters, publication-commit state, and a reconciled authoritative
CandidateStore roster. The child performs that reconciliation before sending
the DTO. Its checksum domain is `APGAR-PHASE4-DURABLE-ARM-FAILURE-V1` and hashes
every field except `payload_checksum` in declaration order. This bounded DTO is
a reconciled diagnostic fingerprint, not a serialization of every heavyweight
attempted-column or CandidateStore object. A typed failure makes the cell
ineligible for publication; investigation of a new invariant class must retain
the corresponding versioned replay artifact before the failure is fixed or
used in a claim.

Payload kind is not a caller-selected label: corpus, sequential,
candidate-preparation, and candidate-session payloads must match their exact
paired summary code and contender arm. Optional case/net/epoch/observation/store
fields use canonical zero values when absent. Corpus work-bound payloads require
their nonzero first-unpreparable-net witness and `required > configured` for
every named limiting bound; other corpus errors must not carry those fields.
An absent non-corpus failed observation also requires a zero attempted-column
count. A present observation has exactly one attempted column per attempted
route query and no more than 1,000,000,000 work units per query. In raw cell
context, a preparation failure is further capped at `N*K` attempted columns and
a session-epoch failure at `N`. Summary-only payloads cannot carry codes 4, 6,
7, 8, 9, 11, or 12; code 10 remains the worker's external-authority setup
failure, and valid-configuration persistent-preparer resource exhaustion uses
code 13. Publication-committed preparation state must match the retained
authoritative-store presence exactly.

For each contender arm, publication requires every repetition's semantics to
be byte-structurally identical after removing only `execution_order`,
`repetition_index`, and `semantic_checksum`. The derived paired comparison must
also be identical across repetitions. Timing, lifecycle, and parent resource
observations remain artifact data and are not part of this deterministic
semantic comparison.

## Parent authority and host environment

Process identity, dispatch ordinal, elapsed time, exit state, watchdog action,
peak RSS, authority checksum, and final pairing are parent-only facts. A child
success remains untrusted until the cleanly reaped parent associates the shared
lifetime peak and passes the execution through `FinalizePhase4TrialArmV1`.

The raw cell also records uname OS/kernel/architecture, first `/proc/cpuinfo`
model name, compiler identity, monotonic-clock name, online and affinity CPU
counts, and `/proc/meminfo` total memory. The environment checksum domain is
`APGAR-PHASE4-HOST-ENVIRONMENT-V1`; it hashes schema then these fields in
declaration order. These labels describe the run and do not alter semantic
routing identity.

## Canonical JSON and checksums

`phase4_evidence_runner` emits one compact UTF-8 JSON object followed by one LF.
Keys are written in the fixed source order, integers are decimal, booleans are
lowercase, and strings use only JSON-required escaping. The root includes the
source commit/stamp state, complete cell configuration and environment, corpus
and controller identities, every pair/arm attempt, successful records and
paired result, durable failure if present, and all checksums.

`source_envelope_checksum` uses domain
`APGAR-PHASE4-SOURCE-ENVELOPE-V1` and binds the wire schema, exact source commit,
stamped and dirty flags, and the complete cell artifact checksum. Publication
validation additionally requires `--expected-commit` from an independent
caller and rejects a mismatch even when the artifact is internally
self-consistent.

Arm-attempt, pair-attempt, and cell-artifact checksum domains are respectively:

```text
APGAR-PHASE4-ISOLATED-ARM-ATTEMPT-V1
APGAR-PHASE4-ISOLATED-PAIR-ATTEMPT-V1
APGAR-PHASE4-ISOLATED-CELL-ARTIFACT-V1
```

They hash fixed-width fields in declaration order and include lower-level
artifact or failure checksums rather than ambiguous serialized native state.
The strict validator rejects duplicate or extra JSON keys, non-exact JSON
types, invalid checksums or associations, wrong repetition/order schedules,
process drift, lifecycle discontinuity, unequal per-process peaks, incomplete
attempts, dirty or mismatched source identity, fabricated corpus/case identity,
and noncanonical publication dimensions or JSON bytes.

This raw schema does not define confidence intervals, family aggregation,
candidate-diversity telemetry, hardware manifests beyond the host fields
above, or the Phase 4 exit decision. Those belong to the versioned report and
decision slice.
