# Deterministic Candidate Store Contract v1

The Phase 3 candidate store owns immutable accepted RouteCandidate v1 objects
and immutable CandidateRejection v1 records. It never mutates Board IR,
CompiledBoard, congestion, prices, or allocator state.

A store instance binds to the complete Board/compiler/routing/rule association
set of its first accepted candidate. Later candidates with another association
set are rejected; candidates from stale snapshots are never mixed into the
same per-net pool or returned by `Enumerate(net)`.

## Budgets and admission order

Configuration supplies positive per-net accepted-candidate and accepted-byte
caps plus a bounded rejection-record cap. Checked logical candidate bytes from
the candidate schema are the byte-budget authority. Pinned candidates consume
the same budgets.

Batch admission first normalizes work in stable `(net, candidate ID)` order,
then serializes publication under the store mutex. Concurrent callers observe
linearizable snapshots. One explicit `AdmitBatch` is the deterministic
publication boundary for candidates produced concurrently: its retained pool
and per-item results do not depend on worker completion order. Separate
single-item calls are race-safe and deterministic for their mutex
linearization, but a bounded store does not promise the same final pool across
different linearizations of future calls. Callers requiring schedule-independent
publication must use one batch. Ranking and pruning use total stable keys and
never depend on hash-table iteration, pointer values, or item order within a
batch. Enumeration is sorted by the ranking key and then candidate ID.

## Deduplication and diversity

- Duplicate candidate IDs are rejected, including an identical repeated
  payload.
- Geometry/resource signature matches trigger full collision-safe canonical
  equality before deduplication.
- Exact duplicate geometry or resource footprints retain the better stable
  ranked representative and a structured diagnostic for the other candidate.
- Resource Jaccard is intersection cardinality divided by union cardinality of
  expanded canonical physical-edge keys. Two empty sets are invalid candidates,
  not a special similarity value.
- Geometric overlap v1 is shared physical-edge DBU projection divided by the
  smaller candidate's physical-edge DBU projection.
- Metric dominance requires no worse intrinsic base cost,
  orthogonal/diagonal steps, bends, or vias and at least one strict
  improvement. Request-local scalar policy costs are retained for provenance
  and differential validation but are not compared across policy identities.

Pruning first preserves every valid retention pin, then useful nondominated
representatives, then the best representative of each unique resource
signature, and finally fills remaining capacity by stable rank. When all
requirements cannot fit, admission fails closed with `BudgetExhausted`; pins
are never silently evicted.

## CAN-002 retention seam

Retention pins are store metadata separate from immutable candidates. A
nonzero owner ID may pin one stored candidate idempotently. Unpinning names the
same owner/candidate pair. Pin acquisition that would violate configured
budgets fails. Phase 3 does not define allocator worlds, prices, selection, or
column generation.
