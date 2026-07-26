# Phase 4 Confirmatory Exact-Small Oracle Publication v1

Status: fail-closed confirmatory exact-cell completion authority with a closed
development surface. It is eligible only as an input to later complete Phase 4
confirmatory aggregation. The current `(10100,4)` production result is not
exact-optimal inside its frozen pools, so it produces no artifact under this
contract.

This contract joins one complete Corpus v2 Same-Run Raw Evidence schema 2 /
Wire-2 cell, its Same-Run Decision Telemetry v1 / Wire-2 companion, one
Corpus v2 Wire-2 Per-Net Report Artifact v1, and one Exact-Small Final-Pool
Snapshot v1. It accepts only exact development cell
`(case_id=10100, requested_pool_size=4)`.

## Closed scope and input order

The publication requires an independently supplied clean 40-character lowercase
source commit, four preparation workers, 20 Raw repetitions, and the complete
frozen canonical configuration for `(10100,4)`. Corpus authority, Raw schema,
Raw wire, telemetry wire, and publication authority are explicit; the case ID
must never select or infer them.

The frozen configuration is schema 1, four workers, 20 repetitions, setup /
prepared / cold caps of `300000000000` ns, address-space cap `68719476736`,
peak-host cap `17179869184`, and corpus limits `(4096, 100000000,
8589934592, 250000, 100000)` in field order. The standalone Oracle Artifact
validator requires exact structural equality to these values; a self-derived,
rechecksummed configuration alias is invalid.

Raw input is capped at 64 MiB, the telemetry companion at 32 MiB, the per-net
report at 32 MiB, and the snapshot at 64 MiB. Every input must be a bounded
regular file containing one strict canonical UTF-8 JSON object followed by
exactly one LF. Duplicate or reordered keys, non-finite numbers, wrong scalar
types, excessive nesting, trailing bytes, FIFO/device inputs, and oversize files
fail the invocation without an output prefix.

The CLI enforces this exact open and validation order:

1. open and fully validate Confirmatory Same-Run Raw under Corpus 2;
2. enforce Raw Evidence schema 2, Raw Wire 2, and exact cell `(10100,4)`;
3. only then open and completely join Same-Run Decision Telemetry v1 /
   Telemetry Wire 2;
4. only after that join succeeds, open and structurally join the Corpus v2
   Wire-2 per-net report; and
5. only after the report join succeeds, open and validate the snapshot.

The join requires exact equality of clean source, complete canonical
configuration, corpus and built-case identities, cell plan, Raw artifact and
source envelope, repetition-zero pair and arm references, complete candidate-arm
semantics, candidate telemetry, six-net EntityRef roster, every per-net final
pool and selection field, report artifact and source envelope, candidate session
and pool manifests, capacity identity, and production outcome. Checksum equality
never substitutes for structural validation.

## Confirmatory snapshot authority

The confirmatory production and test-only snapshot runners require explicit
`--corpus_version=2`, `--raw_evidence_schema_version=2`, and
`--raw_wire_schema_version=2`. They accept only `(10100,4)`, four workers, 20
repetitions, repetition zero in baseline-first order, complete nonzero Raw
references, nonzero Raw/report artifact and source-envelope checksums, and the
associated report checksums. Scope and claimed-association rejection occurs
before fixture resolution, representative-case construction, preparer creation,
or candidate execution. Only the test target recognizes the existing
unstamped-source escape.

The producer uses the explicit Corpus v2 canonical-spec, candidate-snapshot,
case, roster, cell-plan, budget, and semantic-validation entry points. It reuses
Exact-Small Snapshot v1's frozen DTO, component limits, JSON key order, and
artifact/source checksum domains. Corpus version is already part of the complete
candidate semantics hashed by that artifact, and the top-level corpus checksum
and case identity are also hashed. A separately selected Corpus v2 builder and
validator are nevertheless mandatory; legacy snapshot publishers remain
Corpus v1-only.

Raw and report fields within the snapshot are claimed associations until this
publication loads the external documents. The Corpus v2 snapshot path accepts
only the Raw-v2/Wire-2 source-envelope domain. A Raw-v1 carrier, Corpus v1
snapshot, foreign or rechecksummed report, another exact-development case, or
another pool is invalid.

## Independent fixed-pool proof

After all external authorities join, the validator reuses every strict shape,
component, product, canonical-pool, candidate reconstruction, resource-capacity,
selection, and outcome check in Exact-Small Snapshot v1. It preflights the exact
six pool sizes before any candidate traversal and rejects a Cartesian product
greater than the case bound or 4096. Empty pools contribute one explicit
no-candidate sentinel; no prefix or truncated pool is admissible.

Before enumeration, the validator invokes only the fixed Bazel runfile
`phase4_confirmatory_exact_small_candidate_admission_replay`. Its bounded binary
wire version 2 explicitly carries Corpus version 2 and case 10100. The helper
rebuilds that Corpus v2 Board/workload from the joined limits and runs
`ValidateCandidatePayloadWithoutProducerEvidence` for every candidate. It
accepts exact EOF, emits no stdout, and exposes no corpus, case, or executable
substitution. The public publisher is a compiled launcher that authenticates
its canonical executable, complete adjacent target-specific standalone
runfiles tree, and hermetic Python interpreter before delegating to a fixed
private Python target through a one-use parent-bound handshake. An enclosing
Bazel runfiles tree may contain an invocation symlink but is never a
Python/data authority. The inner target cannot run directly. The publisher
resolves replay only from the authenticated standalone `.runfiles/_main` tree;
ambient runfiles directories and manifests are never replay authorities. The
nested process test uses a build-only marker action with each launcher's
`FilesToRunProvider` to materialize that exact target-specific standalone tree
before execution. Only the marker enters test data; the enclosing test tree is
never selected as authority. The process test passes from a fresh Bazel output
root in normal and stamped benchmark configurations. The direct stage-one
Python interpreter disables `site` initialization until the rules_python
bootstrap selects that standalone root; stage-two site initialization occurs
only after that selection. The legacy replay target remains Corpus v1-only and
uses the same compiled-launcher boundary.

The independent oracle enumerates every world in
`prod(max(1, pool.candidates.size()))`, expands compressed spans to atomic
resources, applies the serialized binary capacity model, and ranks worlds only
by:

1. maximum selected-net count;
2. minimum total overuse units; and
3. minimum total unweighted intrinsic base cost.

Overused-resource count is independently recomputed for the production world
and canonical witness but never ranks worlds. Equal objective values establish
production optimality even when witnesses differ. The artifact retains the
number of equal-objective optima and the lowest ordered candidate-ID witness.

Same-Run telemetry remains the sole exact-rejection guardrail authority. A
structurally valid `exact_rejection_guardrail_passed=false` value is copied into
the binding and remains publishable authentic negative evidence when the
fixed-pool proof otherwise completes; it is not a malformed oracle and does not
alter fixed-pool enumeration.

Production and optimum objectives must be exactly equal before artifact
construction. A mismatch fails the publisher without stdout and supplies no
`exact_small_oracle_complete=true` authority. It is valid diagnostic evidence
of a failed completion gate, but the confirmatory campaign treats the absent
oracle authority as incomplete, never as an allocator loss. For the current
development fixture, production is `(6,3,112200)` and the unique exhaustive
optimum is `(6,1,159000)`.

## Oracle Artifact v1

Output is one compact canonical UTF-8 JSON object at most 1 MiB with exactly one
trailing LF. Its fixed top-level key order is:

```text
source_commit, source_stamped, source_tree_dirty, source_envelope_checksum,
schema_version, campaign_id, cell_role,
eligible_input_to_phase4_aggregation, standalone_decision_eligible,
statistical_timing_eligible, coverage_complete, exact_small_oracle_complete,
config, corpus_version, corpus_checksum, raw_binding,
same_run_telemetry_binding, per_net_report_binding, snapshot_binding,
candidate_semantic_checksum, cartesian_product, production_objective,
optimum_objective, production_overused_resource_count,
canonical_witness_overused_resource_count, production_is_optimal,
optimum_count, canonical_witness, artifact_checksum
```

The fixed flags and identities are:

- `schema_version=1`;
- `campaign_id="phase4_confirmatory_corpus_v2"`;
- `cell_role="exact"`;
- `eligible_input_to_phase4_aggregation=true`;
- `standalone_decision_eligible=false`;
- `statistical_timing_eligible=false`;
- `coverage_complete=false`;
- `exact_small_oracle_complete=true`;
- `corpus_version=2`; and
- `corpus_checksum=4182833841936446798`.

`exact_small_oracle_complete=true` means only that this one cell has a complete
admitted fixed-pool proof. It does not mean that exact-cell, matrix, campaign, or
Phase 4 coverage is complete.

`raw_binding` has fixed order:

```text
authority, raw_evidence_schema_version, wire_schema_version,
cell_plan_checksum, artifact_checksum, source_envelope_checksum
```

Its authority is `phase4_confirmatory_same_run_raw_evidence_v1`, with Raw
Evidence schema 2 and Wire 2.

`same_run_telemetry_binding` has fixed order:

```text
authority, schema_version, raw_evidence_schema_version,
raw_wire_schema_version, telemetry_wire_schema_version,
raw_artifact_checksum, raw_source_envelope_checksum, artifact_checksum,
source_envelope_checksum, exact_rejection_guardrail_passed
```

Its authority is
`phase4_confirmatory_same_run_decision_telemetry_v1`, with telemetry schema 1
and Raw/telemetry Wire 2.

`per_net_report_binding` has fixed order:

```text
authority, schema_version, corpus_version, raw_wire_schema_version,
raw_artifact_checksum, raw_source_envelope_checksum, artifact_checksum,
source_envelope_checksum
```

Its authority is
`phase4_confirmatory_same_run_per_net_report_publication_join_v1`, with report
schema 1, Corpus 2, and Raw Wire 2.

`snapshot_binding` has fixed order:

```text
authority, schema_version, corpus_version, raw_artifact_checksum,
raw_source_envelope_checksum, per_net_report_artifact_checksum,
per_net_report_source_envelope_checksum, artifact_checksum,
source_envelope_checksum
```

Its authority is `phase4_exact_small_snapshot_v1`, with snapshot schema 1 and
explicit Corpus 2.

Each objective has fixed order `selected_net_count`, `total_overuse_units`,
`total_intrinsic_base_cost`. The production and optimum objectives must be
equal, `production_is_optimal` is literal `true`, and `optimum_count` is within
the nonzero Cartesian product. The canonical witness contains exactly six
strictly ordered Corpus v2 net EntityRefs, each followed by a candidate ID or
`null`; selected IDs are unique and reproduce the optimum selected-net count.

`artifact_checksum` uses Board IR v1 FNV-1a under
`APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-ARTIFACT-V1`. It hashes the
compact canonical JSON object containing, in fixed order, every field from
`schema_version` through `canonical_witness`. Source stamping and both top-level
checksums are excluded from that preimage.

`source_envelope_checksum` uses
`APGAR-PHASE4-CONFIRMATORY-EXACT-SMALL-ORACLE-SOURCE-V1` over source commit,
stamp state, dirty state, and artifact checksum. Every bound external source
envelope must also be independently derivable from that same clean source and
its bound artifact checksum. These stable hashes are deterministic association
and corruption checks, not cryptographic signatures.

## Decision boundary

This publication proves exact optimality only inside the captured final
candidate pools. It is not Raw outcome or paired-timing authority, not
statistical timing evidence, not proof that the pools contain every legal route,
not combined-board legalization, and not a standalone Phase 4 decision.

The present fail-closed result does not authorize changing the production
selection, copying the oracle witness into the snapshot, weakening objective
equality, or reclassifying the mismatch as a failed decision artifact. Any
production-search remediation must preserve this independent proof and
separately version every changed checksum-bound allocator/configuration and
protocol authority before heldout execution.

The separately published confirmatory same-run operational measurement remains
a sibling exact-cell requirement and is not an oracle input. Exact cases 10101
and 10102, heldout and imported cases, complete aggregation, campaign execution,
and the confirmatory decision remain closed after this development slice.
