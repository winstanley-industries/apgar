# Deterministic Candidate Store Contract v1

The Phase 3 candidate store owns immutable accepted RouteCandidate v1 objects
and immutable CandidateRejection v1 records. It never mutates Board IR,
CompiledBoard, congestion, prices, or allocator state.

## Budgets and admission order

Configuration supplies positive per-net accepted-candidate and accepted-byte
caps plus a bounded rejection-record cap. Checked logical candidate bytes from
the candidate schema are the byte-budget authority. Pinned candidates consume
the same budgets.

Batch admission first normalizes work in stable `(net, candidate ID)` order,
then serializes publication under the store mutex. Concurrent callers observe
linearizable snapshots. Ranking and pruning use total stable keys and never
depend on thread scheduling, hash-table iteration, pointer values, or insertion
sequence. Enumeration is sorted by the ranking key and then candidate ID.

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
- Metric dominance requires no worse scalar cost, orthogonal/diagonal steps,
  bends, or vias and at least one strict improvement.

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

