# Targeted Regeneration Execution v1

> Superseded for production execution by Targeted Regeneration Execution v2.
> Version 1 remains documented for replay identification, but its unbounded
> three-argument CPU A* call is not valid for a composed allocation session.
> Current callers must use schema version `2`.

Targeted Regeneration Execution v1 is the CPU-reference execution and handoff
contract for one immutable `TargetedRegenerationPlan`.

## Input and lifetime

The input is schema version `1`, an exact Board Snapshot, the complete source
One-World request, a move-only targeted-regeneration plan, the exact
CandidateStore named by the plan lease, and:

- maximum CPU route queries;
- maximum deterministic route-work units;
- maximum aggregate policy resource entries; and
- known unmapped exact conflict count.

The configuration limits are positive except for the conflict count. Route
queries are capped at `1,000,000`; policy entries are capped at `100,000,000`.
Each target's complete policy schedule is also capped at the normalized-policy
primitive's `1,000,000` resource entries before any policy storage is allocated.
Route work is the widened sum, per target, of:

```text
requested columns * represented compiled nodes * 9 heading states
```

The Board, canonical workload, and CandidateStore must outlive the synchronous
call and returned result. Other mutations of the same store are serialized by
the caller with the complete invocation.

Before CPU routing, execution requires:

- an active plan lease issued by the supplied store;
- exact Board, compiler, workload, capacity, and request associations;
- exact source request and complete candidate-pool manifests/counts;
- reproducible complete-pool next-price winners for every target;
- a nonzero requested column count no larger than `actions + 1` per target;
- all query, ordinal, compiled-state work, and aggregate policy-entry bounds;
  and
- normalized policies whose complete target-legal prices, intrinsic scaling,
  resources, ordinals, and checksums satisfy Targeted Regeneration Plan v1.

After known scalar caps pass, execution compares the complete expected source
pools and per-net associations against the store through a side-effect-free empty invocation. It
then resolves, validates, bounds, and retains every target's normalized policy
schedule in one complete preparation pass. Only after the whole pass succeeds
may CPU A* process the first column. The source compare is repeated under the
final publication lock. Any execution-preparation failure performs no route
query. Candidate-draft admission preflight necessarily follows generation, but
every such failure still performs no store mutation.

Known source-roster, query, CandidateStore item, and exact projected policy-
entry caps are checked before proportional copies, reserves, or policy resource
allocation. A resource-refinement-only outcome does not reserve target, column,
or publication scratch.

## Authentic generation and publication

Targets use canonical plan order. A stable nonzero batch identity hashes the
domain `APGAR-TARGETED-REGENERATION-CPU-BATCH-V1`, plan checksum, target net,
and target ordinal. Query identities are the one-based global column ordinal.

Each query copies the workload's authentic planar request, replaces only its
candidate policy, runs production CPU A*, and calls the authenticated CPU
candidate builder. Disconnected and unsupported results become structured
candidate rejections. Resource exhaustion, invalid authenticated state, or an
internal route invariant fails execution rather than masquerading as a stall.

All successful drafts and nonfatal generation rejections cross one conditional
CandidateStore invocation transaction. Each draft names its own authentic
compiled view. Before publishing, the store compares every expected source
pool and persistent per-net association under its publication mutex using
canonical net/candidate order, ID, payload checksum, and complete immutable
candidate equality. Drift returns
`store_drift` and changes no pool, rejection history, binding, or telemetry.
Exact admission, diagnostics, ranking, deduplication, pruning, and insertion for
the complete invocation otherwise commit under the existing CAN-004 boundary.

## Refreshed world and leases

After publication, execution enumerates the current complete pool for every
source workload net, preserving explicit empty pools, and reruns One-World at
the plan's next immutable price snapshot. The baseline is the same request and
same price snapshot before new columns.

The result acquires a new atomic CandidateStore group lease over all refreshed
winners before owning and eventually releasing the plan's source lease. An
all-empty plan owns a store-issued zero-pin identity lease; empty refreshed
worlds require no separate successor lease. Runtime lease identities are not
serialized.

## Outcomes

Each scheduled column records net, zero-based per-target column, policy,
batch/query identity, optional candidate ID/payload checksum, optional rejection
code, and one outcome:

1. admitted;
2. duplicate;
3. route disconnected;
4. route unsupported;
5. candidate build rejected; or
6. exact/store admission rejected.

Counters record requested columns, successful routes, built candidates,
admitted candidates, duplicates, rejected columns, novel retained candidates,
changed selections, and successor-pinned candidates.

The primary comparison is lexicographic: maximize selected-net count, minimize
total overuse units, then minimize total intrinsic cost. The disposition and
fixed-precedence terminal reason are:

| Disposition | Terminal reason |
| --- | --- |
| no work | feasible only when overuse and no-candidate counts are both zero; otherwise no targets |
| progress | lexicographically improved |
| stalled | no successful generation; else no novel retained columns; else no selection change; else selection changed without improvement |
| resource refinement required | known exact conflict is not represented by the current resource model |

Invalid input, budget exhaustion, store failure, allocation failure, and
internal invariants are typed execution errors, not stall reasons.

## Replay checksum

Starting from the common FNV-1a offset basis, encode the length-prefixed domain
`APGAR-TARGETED-REGENERATION-EXECUTION-V1`, then:

1. schema `u32` and plan checksum `u64`;
2. the four configuration fields as `u64`;
3. refreshed request manifest, refreshed pool manifest, baseline world, and
   refreshed world checksums as four `u64`;
4. disposition and terminal reason as two `u8`;
5. the nine counters as `u64`;
6. column count `u64`; and
7. for each ordered column: net ID `u64`, generation `u32`, column, policy,
   batch, and query as four `u64`; outcome `u8`; then presence-tagged candidate
   ID high/low, payload checksum, and rejection code.

The representation fixture uses schema/plan `(2, 3)`, configuration
`(5, 7, 11, 13)`, manifests/worlds `(17, 19, 23, 29)`, stalled/no-selection,
counters `(31, 37, 41, 43, 47, 53, 59, 61, 67)`, and one column with net
`(71, 73)`, column/policy/batch/query `(79, 83, 89, 97)`, admission-rejected,
candidate ID `(101, 103)`, payload `107`, and budget-exhausted rejection. Its
checksum is `16407228703171655649`.

## Deliberate boundary

This schema establishes authentic CPU targeted generation, atomic exact
publication, one refreshed One-World decision, explicit stall/refinement
diagnostics, and replay. It does not retain or evolve multiple worlds, define
the Phase 4 corpus, run equal-budget experiments, establish combined exact
board legality, or claim the Phase 4 evidence gate.
