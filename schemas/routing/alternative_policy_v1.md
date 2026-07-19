# Candidate Generation Policy Contract v1

CandidateGenerationPolicy v1 is the backend-neutral, request-local policy used
by CPU A* and every Phase 3 planar GPU generator. It is immutable after
normalization and never mutates Board IR, CompiledBoard, resource usage,
congestion, allocator prices, or another query's policy.

## Associations and identity

The canonical policy carries schema version `1`, an objective identifier, a
deterministic 64-bit seed, a candidate ordinal, nonnegative orthogonal,
diagonal, and bend cost surcharges, a sorted banned-resource set, and sorted
resource penalties. Its `policy_identity` is FNV-1a64 over the domain
`APGAR-CANDIDATE-POLICY-V1` and every canonical field below. The identity is an
association fingerprint, not a cryptographic checksum.

Unknown schema versions and objective identifiers are unsupported. Candidate
ordinal participates in provenance and identity even when two ordinals have
otherwise identical search semantics.

## Planar resource key

Phase 3 v1 names one physical compiled edge with a collision-free structured
key:

1. unsigned 32-bit layer;
2. signed 64-bit canonical source lattice `x` and `y`; and
3. unsigned 8-bit canonical direction in `{east, north-east, north,
   north-west}`.

Traversal in the reverse direction maps to the same key. Keys are ordered by
`(layer, x, y, direction)`. A key outside the associated CompiledBoard, naming
a missing or illegal edge, or using another direction is invalid input rather
than a no-op.

The v1 resource is deliberately a compiled directional-edge capacity unit. It
does not claim portals, via sites, allocator bins, or exact collision ownership.

## Normalization

- Bans are sorted and exact duplicates removed.
- Penalties are sorted by resource key. Duplicate penalties are combined with
  checked unsigned 64-bit addition.
- A banned resource must not also carry a penalty.
- Zero penalties are removed.
- Surcharges and penalties are finite nonnegative integer costs. The associated
  CompiledBoard node bound and maximum transition cost must prove that any
  simple state path fits below `UINT64_MAX`, which remains the unreachable
  sentinel.

The normalized policy is encoded using fixed-width little-endian integers,
explicit vector counts, and the field order in this document.

## Shared CPU/GPU semantics

For a legal transition, both CPU and GPU use:

```text
base compiled step cost
+ objective orthogonal or diagonal surcharge
+ base compiled bend cost when the incoming heading changes
+ objective bend surcharge when the incoming heading changes
+ resource penalty for the canonical physical edge
```

A banned edge is absent, never a large finite cost. Objective v1 identifies
the scalarization/provenance only; its numerical effect is completely captured
by the three recorded surcharges. The admissible heuristic may include the
minimum unavoidable step surcharges but must ignore bend and resource
penalties.

Unsupported policy/backend combinations return `Unsupported`. CPU fallback is
permitted only when it consumes exactly these semantics.

## Deterministic alternative batches

A version-1 alternative batch has a stable batch identity and an ordered list
of unique query identities. Queries may vary candidate ordinal, objective,
seed, bans, penalties, and surcharges while sharing one compatible Board IR,
CompiledBoard, routing profile, rule bucket, endpoint request, and forced
generator. Results are ordered by query identity. Repeating an identical batch
on the same supported backend/device class must produce identical per-query
outcome, scalar cost, geometry, and ordering.

