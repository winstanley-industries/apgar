# Sequential Negotiated-Routing Baseline v1

Sequential Negotiated-Routing Baseline v1 is the named traditional baseline
for the Phase 4 equal-budget comparison. It routes and commits one net at a
time. It does not reuse a candidate pool during search.

## Inputs and ownership

One synchronous invocation receives:

- schema version `1`;
- one exact Board Snapshot and its authentic canonical `MultiNetWorkload`;
- an associated positive-default `ResourceCapacityModel`; zero-capacity sparse
  overrides remain valid;
- a deterministic seed, positive intrinsic-cost weight, and positive sweep
  count;
- positive bounded CPU A* work, record, queue, and reconstruction limits;
- a bounded negotiated-price configuration whose iteration limit covers every
  configured sweep;
- a valid one-item CandidateStore configuration capable of retaining and
  exactly admitting the configured worst-case draft and request policy;
- valid One-World reference limits; and
- positive aggregate session limits for nets, queries, route work, policy
  projection visits, policy resource entries, expanded resource visits,
  occupancy records, candidate draft and current-winner bytes, structured
  rejection bytes, and ordered trace bytes.

The invocation mutates none of these inputs. Its move-only result owns all
candidate handles, pools, traces, worlds, and price state and retains no Board
or workload pointer.

`known_unmapped_exact_conflict_count != 0` returns
`resource_refinement_required` before routing. Version 1 requires a positive
default capacity because it does not enumerate every otherwise-unlisted legal
edge merely to discover zero default capacity.

## Complete preflight

Before the first CPU query, validate Board/workload/capacity associations and
use widened arithmetic to check:

```text
route queries = net count * maximum sweeps
route work = route queries * maximum work units per query
policy projection and merge visits = maximum sweeps *
    (sum of per-net base penalty entries + 2 * net count *
     (maximum price records + maximum occupancy records + capacity overrides))
policy entries = maximum sweeps *
    (sum of per-net base-policy entries + net count * policy roster)
expanded visits = route queries * maximum reconstruction states * 2
trace bytes = route queries * 512 + maximum sweeps * 256
rejection bytes = route queries * 16,384
current-winner bytes = net count * maximum candidate draft bytes
distinct occupancy records = net count * maximum reconstruction states
```

The factor of two conservatively covers prior-route rip-up plus replacement
commit. Queries are capped at `1,000,000,000`, sweeps at `1,000,000`, and
aggregate CPU work at `1,000,000,000,000,000`. Equality is accepted. Runtime
counters enforce the same aggregate limits before the corresponding work is
retained.

Let `R` be maximum reconstruction states, `E` the largest possible normalized
policy entry count for one net, `O` the Board obstacle count, `T` the terminal
count, and `P=S=min(R,1,000,000)`. Before CPU A* can materialize
a route, the following conservative per-query envelopes must fit:

```text
candidate draft bytes = 4,096 + 512 * R + 128 * E
CandidateStore admission input bytes = maximum candidate draft bytes + 57 + 29 * E
CandidateStore admission work =
    1 + P + P * (P - 1) / 2 + P * (O + T) + S + 2 * E + 2 * R
```

Candidate exact geometry spatially prunes self-clearance comparisons, but
CandidateStore transaction accounting charges every possible primitive pair.
The formula therefore uses the full RouteCandidate v1 primitive bound rather
than the exact validator's pair-check limit. These formulas intentionally
overestimate the produced route; equality is accepted and one-under is
rejected before A* runs. The CandidateStore candidate-byte cap must also accept
the complete configured draft maximum.

The workload net count must fit the session and One-World net/candidate limits.
One-World performs its own complete candidate/resource bound validation at
every sweep boundary. The conservative union of occupancy, price, and capacity
records and the worst selected expanded footprints must also fit the configured
One-World limits before routing begins. The same worst selected footprint must
fit the baseline-local distinct occupancy-record bound before CPU A* runs.
Host allocation and container-length failures inside the validated envelope
become bounded structured session errors.

## Sequential sweep

Canonical net order is the immutable workload order. Every completed sweep
performs exactly one query for every net:

1. Remove that net's prior immutable winner from baseline-local occupancy.
1. Freeze one query policy using the occupancy of all currently committed
   other-net routes and the previous completed sweep's price history.
1. Run the bounded production CPU A* once.
1. Build the draft through authenticated CPU producer evidence and exact-admit
   it through a fresh one-item CandidateStore.
1. Commit the admitted immutable replacement into local occupancy before the
   next net. Destroy the ephemeral store; the shared immutable handle survives.

Only the current winner is retained. Older routes are not alternatives and
cannot be selected later. At a later sweep boundary, the previous One-World
result and its singleton-pool handles are released before the next route
generation. Candidate-byte accounting subtracts the replaced route before
adding its replacement and records the peak one-generation winner roster. A
disconnected, unsupported, build-rejected, or admission-rejected attempt
restores the prior winner when one exists. The column retains the exact attempt
outcome and a separate restoration flag.

CPU A* `kWorkBoundExceeded` is an aggregate/per-query work-bound failure, not a
disconnected proof. CPU A* `kResourceExhausted` remains a distinct arithmetic
or host-resource failure. Other invalid, validation, or internal route failures
fail the session. Wall-clock time never changes routing or stopping behavior.

## Query policy

Start from the normalized workload request policy. Derive one stable per-net
seed from the configured seed, workload checksum, and net identity; set the
candidate ordinal to the zero-based sweep index.

The positive intrinsic weight is represented inside A* by adding
`(weight - 1)` times the compiled orthogonal, diagonal, and bend base costs to
the request's existing surcharges. Overflow or the reserved infinite route
cost is rejected.

For each canonical resource in the union of current occupancy, sparse capacity
overrides, and the source price roster, project it into the current net's exact
compiled view. A missing resource contributes nothing. A banned base-policy
resource remains banned. For every retained resource:

```text
prospective_overuse = max(usage_without_this_net + 1 - capacity, 0)
present = min(prospective_overuse * present_step, maximum_price)
congestion_penalty = min(source_history + present, maximum_price)
```

`source_history` is the prior completed sweep's `history_price`; the source
state's old present component is deliberately not reused. A base-policy
penalty on the same resource is added with a finite-cost check. Normalize the
complete immutable policy before search. Congestion penalties are collected in
canonical resource order and linearly merged with the normalized base penalty
vector; inserting entries one by one into the sorted base is forbidden. Both
roster projection and every merge visit are charged to the aggregate policy
projection-work bound.

## Sweep oracle and price transition

After all nets are processed, create exactly one `CandidatePool` per workload
net. It contains the current winner or is explicitly empty. Build the source
price snapshot and run `AllocateOneWorld` over those singleton pools. Because
there is no alternative within a pool, One-World is an independent selected
footprint and resource-accounting oracle, not the baseline search algorithm.

Every positive One-World resource usage must exactly equal baseline-local
occupancy. Then call `UpdateNegotiatedPrices` exactly once. The returned final
world therefore records the last source price iteration; the returned
successor state records the completed update from that world.

## Terminal reasons

Evaluate terminal reasons after the complete world and price update, in this
order:

1. `no_admissible_candidate` when any final pool is empty;
2. `feasible` when every net is selected and total overuse is zero;
3. `fixed_point_stalled` after the first sweep only when every current route's
   exact geometry, resource footprint, intrinsic metrics, constraints, and
   associations equal the preceding sweep and every resource price value,
   observed-overuse value, and clamp flag equals the source state; or
4. `sweep_budget_exhausted` after the configured final sweep.

Candidate ID, policy, scalar policy cost, provenance, query identity, and
pointer identity do not define route semantic equality. This allows a rerun of
the same exact route under a later immutable price policy to be recognized
without pretending the two generated candidates are identical evidence.

## Records and counters

Every query column records sweep, net, normalized policy identity, nonzero
batch identity, one-based query identity, bounded A* work units, the count and
sum of congestion penalties added by the prospective policy, attempt outcome,
prior-retention flag, and optional candidate ID, payload checksum, semantic
checksum, and complete canonical Candidate Rejection v1 record. Outcomes are
admitted, route disconnected, route unsupported, draft build rejected, or
exact/store admission rejected. A retained rejection is re-accounted from its
canonical fields, is individually limited to 16,384 logical bytes, contributes
to the aggregate rejection-byte counter, and participates in the session
checksum without losing stage, associations, provenance, witnesses, text, or
declared logical bytes.

Counters record completed sweeps, queries, route work, successful CPU routes,
admitted candidates, rejected attempts, restored prior routes, policy
projection visits, policy entries, expanded rip-up/commit visits, price
updates, peak retained current-winner candidate bytes, and retained rejection
bytes. Sweep records bind source/successor price checksums, One-World checksum,
query interval, board-level totals, and both fixed-point predicates.

## Stable identities

All identities use the Board IR v1 `StableHashBuilder`. Unsigned integers are
little-endian at their named width; booleans and enum tags are one byte;
strings are a `u64` byte count followed by their bytes.

The batch identity hashes domain `APGAR-SEQUENTIAL-NEGOTIATED-BATCH-V1`, Board
hash, workload checksum, capacity-model checksum, and the complete semantic
configuration in declaration order. The configuration includes every CPU,
price, CandidateStore, One-World, session, and refinement field. Operational
threading and timing are absent.
If the 64-bit batch hash result is zero, it is canonically remapped to `1`
because Candidate Scheduling Identity reserves zero batch identity.

Candidate semantic checksum domain
`APGAR-SEQUENTIAL-NEGOTIATED-CANDIDATE-SEMANTICS-V1` has this exact field order
and width: candidate schema major `u16`, candidate schema minor `u16`, geometry
schema `u32`, resource schema `u32`, net entity (`u64`,`u32`), two terminal
entities in canonical order, the five RouteCandidate associations at their
declared widths, geometry count `u64` and tagged exact primitives, resource
count `u64` and physical spans, all intrinsic metric `u64` fields in
RouteCandidate order, then the four constraint fields (`bool`, `bool`, `u32`,
byte tag). It excludes mutable-generation evidence fields named in the
fixed-point rule. The one-net horizontal representation fixture in the unit
test has semantic checksum `17976206991628397487`.

The session checksum domain `APGAR-SEQUENTIAL-NEGOTIATED-SESSION-V1` encodes
the complete configuration; batch, workload, and capacity identities;
terminal reason; all counters in declaration order; every ordered column and
sweep field in declaration order with presence tags; every final pool's net,
candidate ID, payload checksum, and semantic checksum; final One-World
checksum; and successor price-state checksum. An optional column rejection is
encoded in the exact canonical Candidate Rejection v1 field order and widths
from `schemas/candidate/rejection_v1.md`, including `logical_bytes`. The same
one-net fixture has session checksum `13612100766122023035`.

## Deliberate boundary

This schema defines only the traditional sequential reference. It does not
reuse alternative candidates, execute the complete candidate-allocation
session, define equal-budget wall-time measurement, add a GPU allocator,
legalize combined routes, or claim Phase 4 success.
