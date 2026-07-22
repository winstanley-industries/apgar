# Phase 4 exact-small oracle publication v1

Status: diagnostic publication contract. `decision_eligible` and
`production_is_optimal` are respectively literal `false` and `true` in every
admitted artifact.

This contract joins one complete Isolated Raw Evidence v1 cell, one Per-Net
Report Artifact v1, and one Exact-Small Final-Pool Snapshot v1 for canonical
case 100, 101, or 102 at pool size four. The validator requires an independent
40-character lowercase source commit and runs the complete Raw publication
validator and Raw/report publication join before considering the snapshot.

## Input and join boundary

Raw input is capped at 64 MiB, report input at 32 MiB, and snapshot input at
64 MiB. Each is read once as strict UTF-8 and must be one compact canonical
JSON object followed by one LF. Duplicate keys, non-JSON numeric constants,
wrong integer or Boolean types, unknown enums, trailing bytes, reordered or
unknown keys, and nesting beyond 64 levels fail the whole invocation. No
output prefix is written on failure.

The three-way join requires exact equality of clean source, complete canonical
cell, corpus and built-case identities, Raw cell plan/artifact/envelope,
repetition-zero pair and arm references, complete candidate-arm semantics,
candidate telemetry checksum, full workload EntityRef roster, every per-net
final pool size and selected candidate/status/payload/metric field, per-net
report artifact/envelope, session/final-pool/final-rejection checksums,
capacity identity, and production outcome. Checksums alone never substitute
for a full structural join.

## Independent snapshot validation

The Python validator invokes no allocator, allocation session, or production
scoring implementation. From serialized fields it independently rebuilds:

- normalized generation-policy identity and deterministic candidate ID;
- geometry and compressed-resource 128-bit signatures;
- complete route-candidate payload checksum and canonical logical byte count;
- the ordered per-pool and complete-pool manifests, including empty pools;
- the binary resource-capacity checksum;
- candidate-arm semantic checksum through the shared strict semantic parser;
- exact-small snapshot artifact and source-envelope checksums.

It also enforces the six-pool, six-candidates-per-pool, 36-candidate,
100,000-component/expanded-edge/geometry-step/capacity-row, 32 MiB
candidate-logical-byte, and 4,096-world bounds. Product preflight consumes only
the exact six pool sizes and fails before any candidate payload traversal. A
second shape-only preflight then accounts candidate, geometry, span, policy,
declared logical-byte, expanded-edge, and geometry-step totals before parsing,
expansion, or metric reconstruction. The strict traversal consumes those
preflight budgets exactly. Candidate geometry is canonical nondegenerate
H/V/45 line geometry; policy resources and compressed spans are strict,
bounded, nonoverlapping, maximally coalesced, and int64-safe. The serialized
constraint assessment must be the exact passed v1 shape.

Before enumeration, a bounded C++ replay helper rebuilds the named exact case
from the joined corpus limits and runs the source-private
`ValidateCandidatePayloadWithoutProducerEvidence` seam for every serialized
candidate using that candidate's request policy. This rechecks endpoints,
terminals, layers, Board obstacles, compiler/rule associations, exact geometry,
metrics, and resources without producer authentication and without invoking an
allocator, allocation session, or scoring path. The helper accepts one strict
bounded binary document on stdin, requires exact EOF, emits no stdout, and has
bounded failure diagnostics. Publication therefore does not rely only on the
snapshot producer's earlier replay.

## Exhaustive fixed-pool oracle

An empty pool contributes one explicit no-candidate sentinel. The validator
enumerates every member of
`prod(max(1, pool.candidates.size()))`, expands every selected compressed span
to canonical atomic resources, applies the serialized default/override
capacity, and computes the authoritative three-field objective:

1. maximize selected net count;
2. minimize total overuse units;
3. minimize total unweighted `intrinsic_base_cost`.

`overused_resource_count` is independently recomputed and published for both
the production world and canonical witness, but it never ranks worlds. The
production world is admitted when its three-field objective equals the exact
optimum; its candidate identities need not equal the oracle witness. The
artifact publishes the number of equal-objective optima and the
lexicographically lowest ordered candidate-ID witness (with `null` only for an
empty-pool sentinel).

This proves optimality only inside the frozen candidate pools. It is not proof
that those pools are route-complete, not measured timing evidence, and not a
Phase 4 outcome decision.

## Oracle artifact

The output is one canonical JSON object, at most 1 MiB, with fixed key order:

```text
source_commit, source_stamped, source_tree_dirty, source_envelope_checksum,
schema_version, decision_eligible, case_id, raw_artifact_checksum,
per_net_report_artifact_checksum, snapshot_artifact_checksum,
candidate_semantic_checksum, cartesian_product, production_objective,
optimum_objective, production_overused_resource_count,
canonical_witness_overused_resource_count, production_is_optimal,
optimum_count, canonical_witness, artifact_checksum
```

Each objective contains `selected_net_count`, `total_overuse_units`, and
`total_intrinsic_base_cost` in that order. Witness rows contain complete net
EntityRef then candidate ID or `null`. The artifact checksum domain is
`APGAR-PHASE4-EXACT-SMALL-ORACLE-ARTIFACT-V1`; the source envelope domain is
`APGAR-PHASE4-EXACT-SMALL-ORACLE-SOURCE-ENVELOPE-V1`. Both are deterministic
FNV association/corruption checks, not cryptographic signatures.
