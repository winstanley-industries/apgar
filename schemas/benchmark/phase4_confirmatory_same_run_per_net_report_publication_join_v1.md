# Phase 4 Confirmatory Same-Run Per-Net Report Publication Join v1

This authority binds three independently read canonical artifacts:

1. one complete Corpus v2 Same-Run Raw Evidence v2 / Wire-v2 cell;
2. its complete Same-Run Decision Telemetry v1 companion from the same
   acquisition; and
3. one independently generated Per-Net Report Artifact v1 with
   `raw_wire_schema_version=2`.

The report remains schema version 1. Its payload, artifact checksum,
source-envelope checksum, telemetry schema, and permanent
`decision_eligible=false` value are unchanged.

## Development runner boundary

`phase4_confirmatory_same_run_per_net_report_runner` requires explicit
`--corpus_version=2`, `--raw_wire_schema_version=2`, a clean stamped source,
four workers, 20 repetitions, and every nonzero repetition-zero
baseline-first Raw association field. This implementation slice accepts
exactly case `10100` at pool `4`.

Before fixture resolution, representative-case construction, spec building,
preparer creation, or diagnostic execution, the runner rejects Corpus 1,
Wire 1, every other pool, exact case, calibration, heldout, fixed-query,
stress, and imported case. The ordinary confirmatory report runner remains
restricted to `(10200,4)` and Wire 1.

The diagnostic process rebuilds Corpus v2 case `10100`, executes baseline then
candidate through the explicit Corpus v2 diagnostic paths, validates both
complete semantic objects against the Raw references, and authenticates all
six ordered workload EntityRefs with Workload-Net Roster Manifest v2. The Raw
source association uses Raw-v2/Wire-v2 source-envelope domain
`APGAR-PHASE4-SOURCE-ENVELOPE-V2`.

## Publication join and input order

The publication validator enforces this exact order:

1. read the Raw input as a bounded regular file;
2. completely validate its Corpus v2 Raw-v2/Wire-v2 authority and independently
   supplied clean source commit;
3. reject anything except `(10100,4)`;
4. read the bounded regular sidecar and completely join all Raw cell,
   environment, controller, process, dispatch, pair, arm, outcome, six-net
   roster, column-partition, artifact, and source-envelope identities;
5. only then open the bounded regular report and perform the complete Corpus v2
   structural report join.

The report must exactly bind source, configuration, corpus, cell-plan, Raw
artifact/envelope, repetition-zero pair/arm references, both complete arm
semantics, both telemetry checksums, every per-net closure, and the V2 roster
checksum `12521697377381992336`. The sidecar and report need no direct shared
field because each independently binds the same completely authenticated Raw
artifact and source envelope.

A V1 Raw-v2 join, ordinary V2 Raw/report pair, Wire 1 report, foreign or
rechecksummed sidecar/report, FIFO/device input, missing or reordered net, or
unstamped/cardinality/worker relaxation is rejected.

Raw v2 remains authoritative for allocation outcome and timing. Same-run
telemetry remains authoritative for the exact-rejection guardrail. A valid
nonzero rejection count is failed guardrail evidence, not corrupt evidence,
and the rich diagnostic report cannot offset or reinterpret it. The join
emits no statistic or completion decision and executes no heldout outcome.
