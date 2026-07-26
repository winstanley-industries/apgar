# Phase 4 Confirmatory Same-Run Per-Net Report Publication Join v2

This authority binds three independently read H=4096 Corpus-v2 artifacts:

1. one complete Same-Run Raw Evidence schema-2/Wire-2 exact cell;
2. its complete Same-Run Decision Telemetry schema-1 companion; and
3. one independently generated Per-Net Report Artifact v1 with
   `raw_wire_schema_version=2`.

It supersedes only the configuration-specific
`phase4_confirmatory_same_run_per_net_report_publication_join_v1` authority.
The report payload, telemetry, artifact checksum, source-envelope checksum, and
permanent `decision_eligible=false` value remain unchanged.

## Development runner boundary

`phase4_confirmatory_h4096_same_run_per_net_report_runner` is a separately
compiled authority. It requires explicit Corpus 2, Raw Wire 2, a clean stamped
source, four workers, 20 repetitions, every nonzero repetition-zero
baseline-first Raw association, and exactly exact cell `(10100,4)`.

The runner reconstructs the H=4096 canonical spec out of band and positively
requires equal-arm `present_step_per_overuse_unit=1`,
`history_step_per_overuse_unit=4096`, and canonical algorithm-budget checksum
`8829615204625848656` before source authentication or diagnostic execution.
The existing Corpus-v2/H=2250 same-run runner remains fixed to H=2250 and
cannot select this authority.

Case `10100` is generated. H=4096 same-run report targets declare no board
fixture, accept no fixture path, and resolve no fixture runfile. The production
runner executes baseline then candidate through the private H=4096 same-run
diagnostic path and validates both complete semantic objects and both six-net
telemetry rosters. The test runner is permanently preflight-only and cannot
serialize a report.

## Three-way publication join

`phase4_confirmatory_h4096_same_run_per_net_report_validator` first
authenticates Confirmatory Decision Protocol v2 and its exact same-run per-net
authority substitution. It then enforces this open order:

1. read the Raw input as a bounded regular file;
2. completely validate its H=4096 Raw-v2/Wire-2 authority, canonical budget
   roster v3 identity, exact `(10100,4)` scope, and independently supplied
   clean source commit;
3. read the bounded regular same-run telemetry companion and completely join
   its source, Raw cell, environment, controller, process, dispatch, pair, arm,
   outcome, six-net roster, column partition, artifact, and source-envelope
   identities; and
4. only then open the bounded regular report and perform the complete
   Corpus-v2 structural report join.

The report must exactly bind source, configuration, corpus, cell plan, Raw
artifact and source envelope, repetition-zero pair and arm references, both
complete arm semantics, both telemetry checksums, every per-net closure, and
the ordered workload EntityRef roster. The sidecar and report remain
independent companions of the completely authenticated Raw artifact; no direct
sidecar/report checksum field is added.

Raw, sidecar, report, and independently supplied expected commit must name the
same clean source. H=2250 Raw, ordinary H=4096 Raw/report, Wire 1, a foreign or
rechecksummed sidecar/report, FIFO/device input, or relaxed cardinality is
rejected.

Raw remains allocation outcome and timing authority. Same-run telemetry remains
exact-rejection guardrail authority. A structurally valid nonzero rejection
count is failed guardrail evidence, not corrupt evidence, and the report cannot
offset it. The join emits no statistic or completion decision and opens no
operational, Oracle, heldout, imported, matrix, or decision path.
