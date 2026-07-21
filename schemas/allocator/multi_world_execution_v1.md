# Multi-World Execution v1

Multi-World Execution v1 is the bounded CPU-reference branch and Pareto-
retention contract over one fixed complete candidate-pool snapshot. It composes
One-World Allocation v2 and Negotiated Price State v1 without changing either
primitive and without generating or publishing a candidate.

## Common source and branch state

One execution receives:

- schema version `1`;
- one immutable resource-capacity model and One-World limit set;
- exactly one complete candidate pool, including an explicit empty pool, for
  every net in one nonempty authentic Multi-Net Workload v1;
- one authenticated Negotiated Price State v1 for that exact workload and
  capacity model;
- a bounded nonempty schedule roster;
- the exact CandidateStore containing the complete pool snapshot; and
- one bounded execution configuration.

The branch state is common immutable history. Every schedule starts from its
exact price-state checksum and price snapshot. A later state produced by one
world is never input to another world.

Building the branch Price Snapshot authenticates its capacity associations and
state checksum. Multi-world execution then also requires the branch state's
workload checksum to equal the supplied authentic workload checksum; matching
board/compiler associations alone are insufficient.

Before touching CandidateStore, execution rejects a non-complete or duplicate
pool roster and any candidate whose net, complete derived association, or
canonical geometry endpoint coordinates/layers differ from its exact workload
request. CandidateStore then atomically compares the complete
pool values and persistent per-net associations and leases every source
candidate. While that lease is active, the complete source pools are
authenticated through the production One-World selection boundary. The
resulting canonical pool manifest, workload, capacity-model checksum, and One-
World limits form the source-snapshot replay identity. The source lease
survives until a final conditional lease over the retained winners has been
acquired. An empty winner set produces an active store-identity lease.

Malformed source ownership is a source-pool error. A structurally valid but
omitted, added, or value-replaced complete roster is CandidateStore drift.

CandidateStore may not silently add, remove, replace, or rebind a source-pool
candidate during execution. The final conditional compare detects any such
drift before publishing a result.

## Schedules and rounds

A schedule contains:

- a nonzero unique unsigned 64-bit schedule key;
- one positive unsigned 64-bit search intrinsic-cost weight; and
- one positive unsigned 32-bit maximum selection-round count.

Input order is not semantic. Schedules are processed in ascending key order.
Duplicate keys are rejected. Two different keys with the same weight and round
count are also rejected because they describe duplicate exact v1 searches;
future perturbation fields require a later schema.

One selection round is one complete `AllocateOneWorld` call. Each schedule
copies the common pool handles into its One-World request exactly once, then
replaces only the immutable Price Snapshot between rounds. Round zero uses the
supplied branch state. After an infeasible round, execution calls the production
`UpdateNegotiatedPrices`, builds that state’s exact snapshot, and proceeds.

The schedule stops at the first of:

1. `feasible`: no missing candidate outcome and zero total overuse;
2. `no_candidate_without_regeneration`: at least one workload net has no
   candidate in the fixed pool; or
3. `selection_round_limit`: the declared number of rounds has executed.

Thus a schedule with `R` rounds performs at most `R - 1` price updates. Its
round count is rejected unless those updates fit the branch state’s remaining
Negotiated Price v1 iteration bound. No heuristic, timing, thread completion,
or floating-point stall test changes the schedule.

Each world identity is the byte-stable hash of domain
`APGAR-MULTI-WORLD-IDENTITY-V1`, source-snapshot checksum `u64`, branch-state
checksum `u64`, schedule key `u64`, search weight `u64`, and rounds `u32`. The
fixture `(source 2, branch 3, schedule 5, weight 7, rounds 11)` hashes to
`4575393395812064757`. Complete schedules remain available as collision-safe
evidence; the identity hash alone is not trusted as typed equality. Distinct
canonical schedules that nevertheless produce the same 64-bit identity fail
with an internal-invariant collision diagnostic before world execution or
publication, keeping the singular preferred-world identity unambiguous.

## Bounded work

Configuration provides positive caps for:

- worlds;
- total planned selection rounds;
- total candidate evaluations;
- total candidate-resource-span visits;
- total conservative One-World resource work units, including the hidden
  reconstruction inside every price update;
- total net outcomes;
- Pareto comparisons;
- aggregate buffered terminal selection, resource, and price records held
  while exact dominance is decided;
- retained worlds;
- retained selection, world-resource, and price records; and
- unique retained-winner pins.

It also provides inclusive maximum missing-net and total-overuse thresholds for
near-feasible Pareto eligibility, plus a known unmapped exact-conflict count.
The thresholds and conflict count may be zero.

Before schedule or pool bulk is copied, checked widened arithmetic projects
all known charged work. One fixed candidate-processing pass authenticates the
source snapshot. Each negotiated-price update contains one hidden complete
One-World reconstruction, so the charged-pass count is:

```text
price updates             = total planned rounds - world count
source authentication     = 1 charged candidate pass
charged candidate passes  = 1 + total planned rounds + price updates
candidate evaluations     = charged candidate passes * source candidates
candidate span visits     = charged candidate passes * source candidate spans
resource work units       = charged candidate passes *
                          (One-World resource-input limit +
                           maximum expanded selected source-pool edge records)
net outcomes              = charged candidate passes * source pool count
Pareto comparisons        = worlds * (worlds - 1) / 2
```

Preflight is staged against hostile source shapes. CandidateStore validity and
the O(1) pool-count cap are checked first, followed by all four valid One-World
limits and the exact capacity-override plus branch-price input-record count. A
shallow candidate-count pass stops as soon as the smallest store, pin, One-
World, or charged-work cap is exceeded, without dereferencing any candidate.
Resource-span counts use vector sizes and fail before walking an over-cap
vector; expanded edge-count accumulation stops at the first span that exceeds
either the aggregate One-World expanded-use cap or the per-pass multi-world
resource-work cap. An already rejected source cannot force traversal of the
remaining candidate or span roster.

Resource work is an intentionally conservative deterministic charge, not a
claim that every implementation loop exposes a literal record-visit count.
The per-pass charge is the configured maximum number of One-World resource
records plus, for each source pool, the greatest sum of physical-edge counts
on one candidate. Runtime counters add one complete source pass, then that
charge once for every explicit `AllocateOneWorld` pass and once for the hidden
reconstruction inside every completed `UpdateNegotiatedPrices` call. Thus the
runtime counter equals `(1 + selection rounds + price updates) * per-pass
charge`; early termination reduces the branch passes but never changes the
charge for a pass.

Known-work tuples fail closed unless zero worlds has exactly zero rounds and
zero updates, every positive world count has positive rounds, planned rounds
are at least the world count, and planned price updates equal
`rounds - worlds`. The refinement tuple
`(worlds 0, rounds 0, updates 0)` is valid and still charges its one source-
authentication pass. The total-selection-round cap applies only to declared
branch rounds, not to source authentication.

The complete source pool count and candidate count must also fit the
CandidateStore expected-pool/expected-candidate transaction limits, and the
full source candidate count must fit one pin-lease transaction, before request,
expected-pool, or source-pin vectors are copied.

Equality with every bound is accepted. Overflow or one-over fails with a
structured work-bound error. One-World and Negotiated Price retain their own
per-call bounds. Exact terminal selection/resource/price record totals are
checked before each full terminal world enters the temporary `completed`
buffer; these are distinct from retained-frontier caps. Exact retained record
and unique-winner counts are checked before successor-lease acquisition; the
exact Pareto frontier is never silently approximated to fit a cap.

## Pareto eligibility, dominance, and preferred world

A terminal world is eligible only when:

```text
no_candidate_net_count <= maximum_near_feasible_missing_nets
total_overuse_units     <= maximum_near_feasible_overuse_units
```

Every eligible world is compared in the common coordinate system:

1. selected-net count, maximized;
2. total overuse units, minimized; and
3. unweighted total candidate intrinsic base cost, minimized.

The last coordinate is deliberately not One-World total selection score: that
score includes schedule-specific search weights and negotiated prices and is
not comparable across worlds.

World `A` dominates `B` only when `A` is no worse in all three coordinates and
strictly better in at least one. Equal objective points are retained. Schedule
key, identity, arrival order, pointer identity, and price-state checksum never
turn equality into dominance.

ALL-002 requires the Pareto archive, while the Phase 4 evidence decision is
lexicographic. The result therefore also names one preferred retained world by
selected nets descending, overuse ascending, unweighted intrinsic cost
ascending, then schedule key ascending. The preference does not remove other
nondominated worlds.

Only retained worlds own complete final Negotiated Price states, One-World
selections, resource accounting, and candidate handles. Every scheduled world
retains a compact summary with its schedule, terminal reason, objective tuple,
eligibility/retention flags, and one ordered `(round, state checksum, world
checksum)` trace record per executed round.

## Resource refinement

A nonzero known unmapped exact-conflict count produces
`resource_refinement_required`. It validates and leases the source snapshot,
runs no world, retains no Pareto outcome, makes no feasibility claim, and
returns an active empty store-identity successor lease. Known exact conflicts
outside the physical-edge vocabulary are never relabeled convergence or stall.
Schedule values, uniqueness, iteration compatibility, and CandidateStore
source transaction caps are still validated. The short circuit charges one
source-authentication candidate/span/resource/net pass, but zero selection
rounds, price updates, or Pareto comparisons. Execution-work caps therefore
cover the authentication that actually runs without charging skipped branches.

## Replay encodings

All checksums use 64-bit FNV-1a, offset basis `14695981039346656037`, prime
`1099511628211`, fixed-width little-endian integers, one-byte booleans/enums,
length-prefixed UTF-8 domains, and `u64` vector counts.

The source snapshot domain is `APGAR-MULTI-WORLD-POOL-SNAPSHOT-V1`. Encode
schema `u32`; Board hash `u64`; compiler fingerprint `u64`; compiler version
`u32`; workload checksum `u64`; capacity checksum `u64`; four One-World limits
as `u64`; pool-manifest checksum `u64`; source pool count `u64`; and source
candidate count `u64`. The fixture with schema/associations `(2,3,5,7)` and
remaining fields `(11,13,17,19,23,29,31,37,41)` hashes to
`8008195133944007559`.

The execution domain is `APGAR-MULTI-WORLD-EXECUTION-V1`. Encode:

1. schema `u32`, source-snapshot checksum `u64`, branch checksum `u64`;
2. the eighteen configuration fields in public declaration order as `u64`;
3. disposition `u8`;
4. the twelve counters in public declaration order as `u64`;
5. preferred-world presence `u8` and, when present, identity `u64`;
6. schedule count `u64`, then each schedule as key `u64`, weight `u64`, rounds
   `u32`;
7. summary count `u64`, then for each summary: identity; complete schedule;
   terminal reason `u8`; trace count and every round `u32`, state checksum
   `u64`, world checksum `u64`; selected, missing, overused-resource, total-
   overuse, and intrinsic totals as five `u64`; eligible and retained bytes;
8. retained count `u64`, then identity, final state checksum, and final world
   checksum as three `u64` per retained world.

The representation fixture uses schema/source/branch `(2,3,5)`; configuration
primes `7` through `73`; no-eligible-world disposition; counter primes `79`
through `137`; preferred identity `139`; schedule `(113,127,131)`; summary
identity `137`, round-limit reason, trace `(139,149,151)`, objective/status
fields `(157,163,167,173,179,true,false)`; and retained replay
`(181,191,193)`. Its checksum is `2021987165982029810`. Any field, decision
rule, order, or byte-
encoding change requires a new schema version.

## Deliberate boundary

V1 does not interleave targeted regeneration, share new columns between worlds,
publish or prune candidates, perturb prices randomly, run on GPU, validate
combined exact route geometry, legalize, invoke host CAD, or establish Phase 4
evidence-gate success. Shared-store regeneration requires a later epoch barrier
that freezes pools, gathers all world requests, performs one canonical CAN-004
publication, and refreshes every world from the same successor snapshot.
