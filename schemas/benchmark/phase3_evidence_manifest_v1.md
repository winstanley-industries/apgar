# Phase 3 Published Evidence Manifest v1

The manifest binds one committed Phase 3 Google Benchmark JSON artifact to its
raw-file SHA-256, exact 40-character source commit, human-readable report, and
dispatch ADR. `result_sha256`, `report_sha256`, and `decision_sha256` are
mandatory lowercase SHA-256 digests of the exact bytes in the files named by
their corresponding path fields. Paths are repository-relative, must not
contain `..`, and identify ordinary files. The manifest schema identity is
`phase3_evidence_manifest_v1`. `result_schema` is mandatory and names either
the historical `phase3_candidate_bakeoff_v1` contract or the stricter
`phase3_candidate_bakeoff_v2` contract; validators apply version-specific
required context and counters and never infer v2 evidence from v1 fields.

The evidence validator independently checks the result contract rather than
treating manifest summary values as measurements. It requires the complete
eleven-case, six-pool-size, four-generator, three-stage matrix plus eleven
prepared-upload rows; all four Google Benchmark aggregates; required context
and counters; counter/accounting identities; deterministic ordered differential
agreement; and stable failure semantics. It independently compares reachability
and scalar-cost summaries to sequential CPU A* for v1, and additionally compares
the ordered policy/failure/cost semantic checksum for v2; the producer's
`differential_match` flag is never sufficient by itself. It recomputes
end-to-end and execution-only winners from median real times and recomputes the
generator/stage median count, unique requested,
reachable, and unreachable query totals, and total failures. The manifest's
mandatory `expected_correctness_summary`, `expected_end_to_end_wins`, and
`expected_execution_wins_against_fastest_cpu` objects must match those derived
values. The validator hashes the report and ADR before parsing their
conclusions, then also verifies that both name the bound artifact, result
checksum, commit, and their applicable recomputed conclusions. Substring
agreement is not an artifact binding: validation fails if any byte of either
document changes without a corresponding manifest digest update. The same
summary derivation applies to historical v1 and strict v2 result artifacts; no
v2-only field is inferred while validating v1.

Any result replacement requires a new clean, VCS-stamped benchmark run, a new
raw-file checksum, synchronized report and ADR conclusions, and a manifest
update. A checked-in JSON file, Markdown claim, or caller-provided commit label
alone is not durable benchmark evidence.

The canonical hermetic validation command is:

```sh
bazel test //:phase3_evidence_test //:phase3_evidence_corruption_test
```
