# Phase 4 exact-small oracle publication v2

Status: diagnostic publication contract. `decision_eligible` and
`production_is_optimal` are respectively literal `false` and `true` in every
admitted artifact.

This contract joins one complete Same-Run Raw Evidence v2 cell, its Same-Run
Decision Telemetry v1 companion, one Wire-v2 Per-Net Report Artifact v1, and
one Exact-Small Final-Pool Snapshot v1 for canonical case 100, 101, or 102 at
pool size four. It supersedes Oracle Artifact v1 only for the exact cells
governed by Statistical Decision Protocol v3.

## Input and authority ordering

Raw input is capped at 64 MiB, the telemetry companion at 32 MiB, the report at
32 MiB, and the snapshot at 64 MiB. Each input is strict canonical UTF-8 JSON
with one trailing LF. Duplicate keys, non-finite numbers, wrong scalar types,
unknown or reordered fields, excessive nesting, and trailing bytes fail the
whole invocation. No output prefix is written on failure.

The validator reads and fully validates Raw v2 and its same-run companion
first. Only then may it read and validate the Wire-v2 report, and only after
that join succeeds may it read the snapshot. The publication requires one
independently supplied 40-character lowercase source commit and joins the
complete clean source, canonical cell, Raw artifact/envelope, telemetry
artifact/envelope, report artifact/envelope, repetition-zero references,
candidate-arm semantics, per-net telemetry, and snapshot. A checksum match is
never a substitute for the structural join.

Negative exact-rejection telemetry remains valid same-run evidence. The oracle
proves a property of admitted frozen candidates; it does not reinterpret a
nonzero exact-validation rejection count as missing evidence.

## Independent fixed-pool proof

After versioned authorities are validated, this contract reuses every
independent snapshot reconstruction, candidate-admission replay, shape/product
bound, and exhaustive enumeration requirement in
`phase4_exact_small_oracle_publication_v1.md`. It ranks worlds only by maximum
selected-net count, minimum total overuse units, and minimum unweighted
intrinsic base cost. It publishes overused-resource counts only as diagnostics.

The result proves production optimality only within the frozen final candidate
pools. It is not timing evidence, does not prove those pools route-complete,
and does not decide Phase 4.

## Oracle Artifact v2

Output is one compact canonical JSON object at most 1 MiB. In addition to the
v1 objective and witness fields, it binds:

- Raw evidence and wire schema versions plus Raw artifact/source envelope;
- telemetry schema and wire versions plus telemetry artifact/source envelope;
- Per-Net Report artifact/source envelope; and
- Exact-Small Snapshot artifact/source envelope.

The full fixed key order is:

```text
source_commit, source_stamped, source_tree_dirty, source_envelope_checksum,
schema_version, decision_eligible, case_id, raw_evidence_schema_version,
raw_wire_schema_version, raw_artifact_checksum, raw_source_envelope_checksum,
same_run_telemetry_schema_version, same_run_telemetry_wire_schema_version,
same_run_telemetry_artifact_checksum,
same_run_telemetry_source_envelope_checksum,
per_net_report_artifact_checksum, per_net_report_source_envelope_checksum,
snapshot_artifact_checksum, snapshot_source_envelope_checksum,
candidate_semantic_checksum, cartesian_product, production_objective,
optimum_objective, production_overused_resource_count,
canonical_witness_overused_resource_count, production_is_optimal,
optimum_count, canonical_witness, artifact_checksum
```

Each objective contains `selected_net_count`, `total_overuse_units`, and
`total_intrinsic_base_cost`. The witness contains exactly six strictly ordered
net EntityRefs and a candidate ID or `null`. The artifact checksum domain is
`APGAR-PHASE4-EXACT-SMALL-ORACLE-ARTIFACT-V2`; the source envelope domain is
`APGAR-PHASE4-EXACT-SMALL-ORACLE-SOURCE-ENVELOPE-V2`. These FNV checksums are
deterministic association and corruption checks, not cryptographic signatures.
