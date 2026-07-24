# Phase 4 Confirmatory Per-Net Report Publication Join v1

This authority binds one complete ordinary Corpus v2 Raw/Wire-v1 cell to one
independently generated Per-Net Report Artifact v1. The report payload,
artifact checksum, source-envelope checksum, telemetry schema, and permanent
`decision_eligible=false` value remain unchanged; the join selects Corpus v2
explicitly rather than inferring it from a case ID.

## Development runner boundary

`phase4_confirmatory_per_net_report_runner` requires
`--corpus_version=2`, `raw_wire_schema_version=1`, a clean stamped source, four
workers, 20 repetitions, and every nonzero repetition-zero baseline-first Raw
association field. This implementation slice accepts exactly case `10200` at
pool `4`. It rejects every other calibration pool/case and every exact,
heldout, fixed-query, stress, or imported case before representative-case
construction, preparer creation, or diagnostic arm execution.

The separate diagnostic process rebuilds Corpus v2 case `10200`, executes
baseline then candidate through the explicit Corpus v2 diagnostic entry
points, validates both complete semantic objects against the Raw references,
and authenticates all 64 ordered workload EntityRefs with Workload-Net Roster
Manifest v2. The ordinary Raw source association remains Raw-v1/Wire-v1; the
Corpus v2 cell-plan, root, descriptor, case, workload, capacity, budget, and
roster domains are mandatory.

## Publication join

`phase4_confirmatory_per_net_report_validator` first reads and completely
validates the Raw file against Representative Manifest v2, Workload-Net Roster
Manifest v2, Confirmatory Decision Protocol v1, canonical 20-repetition and
four-worker cardinality, and an independently supplied clean source commit. It
requires the exact ordinary `(10200,4)` cell before opening the report file.

Only then may it read the bounded regular report file and require exact equality
of source, configuration, corpus, cell-plan, Raw artifact/envelope,
repetition-zero pair/arm references, and both complete diagnostic semantics
objects. It independently recomputes the report artifact/envelope, both
telemetry checksums, every per-net closure, and the V2 roster checksum. A V1
Raw/report, Wire 2, foreign V2 cell, FIFO/device input, missing or reordered
net, or rechecksummed cross-authority substitution is rejected.

The report contains no measured timing, lifecycle, utilization, CPU, RSS, or
memory observation. It cannot replace Raw outcome/timing authority or
same-run exact-rejection authority and emits no statistic or completion
decision. No heldout outcome is executed by this slice.
