# Phase 4 exact-small final-pool snapshot v1

Status: diagnostic evidence schema. `decision_eligible` is always `false`.

The legacy Corpus v1 artifact is emitted only for canonical exact-oracle cases
100, 101, and 102 at requested pool size 4 and repetition 0 in baseline-first
order. The separately selected confirmatory Corpus v2 path reuses this frozen
payload shape only for development cell 10100 at pool 4 under
`phase4_confirmatory_exact_small_oracle_v1`. Both are fresh diagnostic reruns
associated with, but never substituted for, the version-matched Raw cell and
per-net report named by their checksum fields.

## Proof boundary

The artifact freezes the production candidate arm's complete final candidate
pools and current resource-capacity vocabulary. It supports later independent
enumeration of the Cartesian product of those pools. It does not prove that the
pools contain every legal route, that a selected route is legal in combined
board geometry, that the sequential baseline is optimal, or that any allocator
schedule is globally optimal outside the frozen pools.

`cartesian_product` is exactly `prod(max(1, pool.candidates.size()))`; an empty
pool contributes one explicit no-candidate sentinel. The producer computes the
product with widened arithmetic from pool sizes before dereferencing any
candidate or traversing any candidate payload. Products greater than the case
descriptor's declared maximum or 4096 fail with a typed error. Truncation and
bounded-prefix serialization are forbidden.

The v1 shape is frozen at exactly six pools, at most six candidates per pool,
36 candidate rows, 100,000 geometry primitives, 100,000 resource spans,
100,000 generation-policy resource records, 100,000 expanded atomic edges,
100,000 capacity-override rows, 32 MiB of aggregate canonical candidate logical
bytes, and 64 MiB of canonical JSON. Vector counts are preflighted before
element traversal; expanded edges are accumulated only after the span-count
bound passes. Component and output excesses fail with typed errors; a producer
writes stdout only after the complete bounded JSON string exists.

## Authority and identity

The top-level object binds the complete canonical cell, source commit/envelope,
case/board/workload identities, full EntityRef workload roster, root seed,
execution order, paired budget checksum, preparation worker count,
version-matched Raw cell and repetition-zero pair references, and per-net report
artifact/envelope. It also deep-copies and binds the complete candidate-arm
semantics row, not only its projected semantic/session/manifest checksums. The
full row includes explicit corpus version, work opportunity and actuals,
preparation and regeneration partitions, requested/admitted/rejected columns,
final candidate count, terminal reason, outcome source, complete outcome, and
semantic checksum. Projected top-level fields must exactly equal that
structurally validated row.

Corpus authority is selected out of band by separate builder, validator, runner,
and publication entry points; a case ID never selects or infers it. Legacy
Corpus v1 producers accept exactly a Raw-v1/Wire-v1 or Raw-v2/Wire-v2 carrier
for cases 100-102. The confirmatory Corpus v2 producer accepts only
Raw-v2/Wire-v2 for case 10100 in this slice. Every legacy publisher and
candidate-admission replay target remains Corpus v1-only; the confirmatory
publication uses a fixed separately compiled Corpus v2 replay target.

Raw and per-net report checksum/envelope fields are claimed cross-artifact
associations. This in-process producer checks their required nonzero,
authority-selected cell-plan, roster, semantics, and source-envelope shape but
does not possess either external artifact document; publication remains
responsible for loading those documents and performing the complete ordered
join. The Corpus v2 selector does not change the snapshot v1 DTO, component
bounds, canonical JSON, or either snapshot checksum domain because the corpus
version, corpus checksum, case identity, and complete semantics are already
hashed.

The current capacity model is serialized as its schema and associations,
default capacity, complete strict resource-key-ordered binary overrides, and
recomputed capacity-model checksum. No negotiated prices are part of the exact
enumeration vocabulary. The validator independently expands selected candidate
spans, accumulates current usage, applies the default/override capacity, and
reproduces resource-count and unit-count overuse totals.

## Pools and candidates

There is exactly one pool for every workload net, including explicit empty
pools. Pools are ordered by full `(net.id, net.generation)`. Candidates within
each pool are ordered by `(candidate_id.high, candidate_id.low)`. Candidate IDs
are unique across the artifact; equal payload checksums are not forbidden. The
validator recomputes the exact One-World two-level final-pool manifest from the
full ordered roster, including empty pools, and requires it to equal both the
captured semantic manifest and its top-level projection.

Each candidate row deep-copies every field hashed by
`APGAR-ROUTE-CANDIDATE-V1`, excluding only the opaque producer-evidence handle:

- schema, ID, net, intended terminals, and all associations;
- geometry/resource schema versions, complete normalized generation policy,
  policy identity, and complete generator/backend/device/seed provenance;
- complete geometry primitives and compressed physical resource spans;
- metrics, constraint assessment, geometry and resource signatures;
- payload checksum, logical byte count, and unweighted intrinsic base cost.

The strict typed validator reconstructs `GeneratedRouteCandidate` and reuses
the source-private non-authenticating part of exact admission to recompute the
candidate ID, policy identity, geometry signature, resource signature, payload
checksum, logical bytes, complete metrics and constraints, geometry legality,
resource equivalence, provenance enums and UTF-8, and unweighted intrinsic base
cost. It does not construct `RouteCandidate`, mutate `CandidateStore`, or
recreate the deliberately opaque producer-evidence capability. Any
future external-file publication validator must perform the same reconstruction
and exact comparison before trusting costs or resource spans; checking only the
Raw final-pool manifest's ID and payload checksum is insufficient.

Every compressed physical edge span must use one of the four canonical planar
directions, have nonzero edge count, usage one, canonical strict/maximal order,
and int64-safe source and physical endpoint coordinates. Expanded atomic edges
must not overlap within one candidate.

## Production selection

Every net has one explicit production selection status. A selected row carries
candidate ID, payload checksum, and intrinsic cost and must exactly match a
candidate in the corresponding frozen pool. A no-candidate row carries null
identity, zero intrinsic cost, and corresponds to an explicit empty pool. The
top-level production outcome is the preferred retained Multi-World when one is
available; otherwise `production_outcome_source` documents the common-lineage
One-World fallback.

## Canonical JSON

Serialization is one compact UTF-8 JSON object with the fixed implementation
key order and exactly one trailing LF. All integers are base-10 JSON integers;
absent selection identities are JSON `null`. `artifact_checksum` covers every
semantic field except source stamping and the two top-level checksum fields.
`source_envelope_checksum` binds source commit, stamp state, dirty state, and
artifact checksum. These FNV-based checksums are deterministic association and
corruption checks, not cryptographic signatures.
