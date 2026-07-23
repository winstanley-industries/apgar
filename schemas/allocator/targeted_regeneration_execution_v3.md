# Targeted Regeneration Execution v3

> Superseded for production execution by Targeted Regeneration Execution v4.
> This frozen document preserves the version-3 seeded replay encoding. Current
> production callers reject versions `1`, `2`, and `3` and must use schema
> version `4`.

Targeted Regeneration Execution v3 preserves the immutable-plan, conditional
CAN-004 publication, refreshed-world, lease, and terminal semantics of version
1 while making every CPU query and all unpublished candidate work suitable for
a composed allocation session.

## Version and inputs

The historical contract accepts schema version `3`; versions `1` and `2` are rejected. One synchronous
invocation receives the exact version-1 plan, Board Snapshot, complete source
One-World request, CandidateStore named by the plan lease, and this semantic
configuration in declaration order:

1. caller-rooted deterministic seed;
2. maximum route queries;
3. per-query CPU A* work units, record count, queue size, and reconstruction
   states;
4. maximum aggregate CPU A* work units;
5. maximum policy-projection roster visits;
6. maximum aggregate policy resource entries;
7. maximum logical bytes for one generated candidate draft;
8. maximum aggregate logical bytes for unpublished generated drafts;
9. maximum aggregate logical bytes retained in canonical rejection records;
10. maximum transient result bytes for columns and rejection copies; and
11. known unmapped exact-conflict count.

Every limit is positive except the conflict count. Queries are capped at
`1,000,000`, aggregate and per-query route work at
`1,000,000,000,000,000`, policy-projection visits at
`1,000,000,000,000,000`, and policy entries at `100,000,000`. Canonical
rejections are individually capped at 4,096 logical bytes. The complete
CandidateStore configuration is snapshotted into the result and replay
identity. Its item, expected-pool/candidate, rejection, input-byte, exact-work,
and pin authorities remain independent.

## Allocation-free preflight

Known scalar and O(1) container bounds are checked before proportional copies,
policy allocation, or CPU routing. Source pool and candidate counts must both
match the plan before any candidate footprint is traversed, and must fit the
CandidateStore expected-roster limits. Requested columns must fit the route,
admission-item, rejection-item, and unsigned-32-bit ordinal domains.

Let `Q` be total requested columns; `W` per-query maximum CPU work; `R`
maximum reconstruction states; `E` the exact aggregate projected policy-entry
count; `Emax` the greatest conservative entry count for one column; `O` and
`T` Board obstacle and terminal counts; `H` the target count; `N` the complete
negotiated-price roster count; `C` the source-candidate count; and
`P = S = min(R, 1,000,000)`. Widened arithmetic checks:

```text
aggregate route work = Q * W
policy projection visits = 4 * H * N

candidate draft bytes = 4,096 + 512 * R + 128 * Emax
generated draft bytes = Q * configured candidate-draft byte cap
rejection record bytes = Q * 4,096
transient result bytes = Q * (256 + 2 * 4,096)

CandidateStore input bytes = Q * (configured candidate-draft byte cap + 57)
                           + 29 * E

CandidateStore exact work = Q * (1 + P + P * (P - 1) / 2
                                 + P * (O + T) + S + 2 * R)
                          + 2 * E

refreshed One-World candidates = source candidate count + Q
refreshed expanded uses = source expanded edge uses + Q * R
refreshed resource records = capacity overrides + next-price records
```

The four policy-projection passes cover preflight, synthesis validation,
projection, and materialization. Their full `H * N` visits are charged before
the first price-roster scan and retained in the execution counter. This makes a
configured visit failure allocation-free and independent of where a matching
price appears.

For one target, `Emax` is its target-legal price count plus one when a later
forced-ban column exists. This may conservatively charge one extra record when
the ban replaces a price; it never undercounts. The store input formula uses
the wider resource-penalty encoding for every policy entry. Equality is
accepted; overflow and one-under fail with `work_bound_exceeded` before A*.

When `Q` is nonzero, the existing rejection count plus `C + Q` must fit the
store history cap so all preexisting diagnostics and every possible invocation
diagnostic remain available. CandidateStore publication can reject an
incumbent source candidate when a generated candidate displaces it, so `C` is
part of this conservative history envelope even though only `Q` candidates are
submitted. When `Q` is zero, both conditional publications are empty and the
envelope is only the existing rejection count; hypothetical incumbent
diagnostics are not reserved. Per-net candidate bytes need not fit the
configured draft cap:
exact admission may deterministically reject or prune a bounded draft as an
ordinary outcome. A nonzero unmapped-conflict count retains the version-1
refinement short circuit and allocates no query or publication scratch.
When `Q` is zero, all query-derived envelopes above are zero and the
single-draft cap is not tested against a hypothetical candidate.

Before routing, the source pool count, worst-case refreshed candidate count,
unchanged resource-record count, and worst-case refreshed expanded uses must
fit the source request's complete One-World limits. Planning already caps `Q`
to `maximum_candidates - source candidate count`; execution rechecks that
compositional equality defensively. The expanded-use envelope is new execution
work because each possible novel route may contain up to `R` physical edge
uses. Equality is accepted; an expanded-use one-under fails before query one
and before CandidateStore mutation. Execution subtracts `Q * R` from the
expanded-use envelope before walking source footprints, counts each inspected
resource span, and stops on the first span that exceeds the remaining source
allowance. A repeated candidate handle therefore cannot force traversal of an
already rejected suffix.

## Bounded execution and atomicity

Every column calls bounded production CPU A*. A bounded A*
`work_bound_exceeded` result remains a typed execution
`work_bound_exceeded`; it is not host exhaustion, disconnection, or stall.
Route-cost arithmetic overflow remains `resource_exhausted`. All preceding
outputs remain caller-owned scratch, so any fatal prepublication failure
changes no CandidateStore pool, rejection history, pin state, or telemetry.

Each attempted column retains optional complete CPU Route Telemetry v1: queue
pops, expanded states, attempted and accepted relaxations, peak record count,
peak queue size, and work units. It also retains exact generated-draft logical
bytes, or zero when no draft was built. Every rejected column retains its
complete canonical Candidate Rejection v1, including provenance, associations,
witnesses, detail, and logical bytes; the summary rejection code must agree.
Counters record requested columns, route queries, route work, declared policy
projection visits, peak route records and queue, generated bytes, rejection
record bytes, transient result bytes, successful routes,
built/admitted/duplicate/rejected candidates, novel retained candidates,
changed selections, and successor pins.

The six successful-execution outcomes and the original three observation
stages retain ordinals zero through eight. Version 3 retains the version-2
`rejection_evidence_in_flight` at ordinal nine and
`publication_committed_outcome_correlation_pending` at ordinal ten. A complete
route-request copy is prepared before its column is appended as
`query_in_flight` and before query counters advance, so a preparation exception
does not invent an attempted query. A query advances to `build_in_flight` only
after CPU A* returns an authenticated route, and to
`generated_pending_publication` only after the candidate draft is built.

Before any fallible rejection construction, canonicalization, or copy, its
column advances to `rejection_evidence_in_flight`. Complete canonical evidence
is staged locally and moved into the column before rejection bytes, summary
code, final outcome, or rejected-column counters advance. Thus every final
route-, build-, duplicate-, or admission-rejection outcome has complete
matching evidence; a host exception instead retains the evidence-in-flight
stage and only the counters that were already true. Successful executions
contain none of the five observation-only stages.

Successful drafts and nonfatal rejections still cross exactly one conditional
CandidateStore invocation. Source drift, aggregate admission failure, host
failure during publication staging, or a publication preparation fault changes
no pool, rejection history, pin state, immutable session/net association
binding, or CandidateStore telemetry. Association bindings, rejection-merge
telemetry, publication work telemetry, pools, and the global ID index commit
only with an ordinary pinned-budget diagnostic outcome or the final
authoritative publication. Refreshed pools/worlds, successor leasing,
progress/stall classification, and terminal precedence are unchanged from
version 1.
After CandidateStore returns successfully, every generated-pending column is
converted nonthrowingly to
`publication_committed_outcome_correlation_pending` before the failed-
observation commit bit becomes true. Publication outcomes correlate only from
that stage, by the one-based query identity directly to the zero-based canonical
column index. Execution rejects an out-of-range, duplicate, missing,
misassociated, or wrongly staged result rather than performing an order-
dependent search. A host exception immediately after commit may therefore
retain zero admission/duplicate/rejection counters while the commit bit and
correlation-pending columns truthfully identify the CandidateStore as
authoritative.

Any fatal result after a query begins retains a bounded failed-execution
observation containing schema version, plan checksum, complete execution and
store configurations, a CandidateStore-publication-committed bit, the counters
reached so far, and the ordered attempted column prefix. Pre-query host or
container exceptions and other unexpected exceptions remain plain typed errors
because no query prefix exists. Domain
`APGAR-TARGETED-REGENERATION-FAILED-EXECUTION-V3` hashes those fields, the
execution error code, and every complete column and rejection into the
observation checksum. The failed-observation representation fixture with the
commit bit clear hashes to `11490479365299976412`; toggling the bit changes the
checksum.

The attempted prefix records its truthful last reached stage. In particular,
an earlier generated draft is never labeled disconnected merely because a
later query or publication staging failed, and the reached stage counters agree
with the prefix even though no prepublication candidate entered the store.

When the commit bit is false, the observation is diagnostic caller-owned state
and CandidateStore remains at its pre-invocation snapshot. When it is true, the
atomic publication completed before a later host, refreshed-world invariant,
or successor-lease failure; published candidates and diagnostics remain in the
store and must be retained or explicitly reconciled by the caller. The
refreshed One-World preflight eliminates deterministic allocator-bound failure
from that post-publication region, but it cannot preclude host or unexpected
failure after commit. The successor-pin counter advances only after the complete
successor lease is acquired; a successor-lease failure therefore leaves all
published candidates retained but reports zero successor pins.

## Semantic identities

The per-target batch domain is
`APGAR-TARGETED-REGENERATION-CPU-BATCH-V3`. It encodes plan checksum, target net,
target ordinal, complete execution configuration beginning with the caller
root seed, and complete CandidateStore configuration. The derived nonzero
batch identity is then the deterministic seed supplied to that target's
normalized policy schedule. A zero result is remapped to one.

Execution domain `APGAR-TARGETED-REGENERATION-EXECUTION-V3` encodes:

1. schema, plan checksum, the fourteen execution scalars, and ten store scalars;
2. refreshed request/pool manifests and baseline/refreshed world checksums;
3. disposition, terminal reason, and seventeen counters; and
4. every ordered column: net and scheduling identities, telemetry presence and
   all seven telemetry values, draft bytes, outcome, and presence-tagged
   candidate ID, payload checksum, rejection code, and complete canonical
   rejection.

The representation fixture hashes to `10370796152289992323`. Any field, order,
width, or decision change requires a new version. Addresses, lease IDs, timing,
and interleaving are excluded.

## Deliberate boundary

Version 3 closes the bounded-execution prerequisite for a composed candidate-
allocation session. It does not repeat regeneration epochs, interleave
publication with multi-world execution, run a GPU generator, validate combined
route geometry, legalize, invoke host CAD, or establish the Phase 4 gate.
