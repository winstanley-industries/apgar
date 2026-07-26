# Phase 4 Confirmatory Per-Net Report Publication Join v2

This authority binds one complete ordinary H=4096 Corpus-v2 Raw Evidence
schema-1/Wire-1 cell to one independently generated Per-Net Report Artifact
v1. It supersedes only the configuration-specific
`phase4_confirmatory_per_net_report_publication_join_v1` authority. The report
payload, telemetry, artifact checksum, source-envelope checksum, and permanent
`decision_eligible=false` value remain unchanged.

## Development runner boundary

`phase4_confirmatory_h4096_per_net_report_runner` is a separately compiled
authority. It requires explicit Corpus 2, Raw Wire 1, a clean stamped source,
four workers, 20 repetitions, every nonzero repetition-zero baseline-first Raw
association, and exactly calibration cell `(10200,8)`.

The runner reconstructs the H=4096 canonical spec out of band and positively
requires equal-arm `present_step_per_overuse_unit=1`,
`history_step_per_overuse_unit=4096`, and canonical algorithm-budget checksum
`8230401457668518004` before source authentication or diagnostic execution.
The existing Corpus-v2 runner remains restricted to H=2250 `(10200,4)` and
cannot select this authority.

Case `10200` is generated. The H=4096 report targets declare no board fixture,
accept no fixture path, and resolve no fixture runfile. The production runner
executes baseline then candidate through the private H=4096 diagnostic entry
point and validates both complete semantic objects and both 64-net telemetry
rosters. The test runner is permanently preflight-only and cannot serialize a
report.

## Publication join and source ancestry

`phase4_confirmatory_h4096_per_net_report_validator` first authenticates
Confirmatory Decision Protocol v2 and its exact ordinary per-net authority
substitution. It then reads and completely validates the H=4096 ordinary Raw
file against canonical budget roster v3, Representative Manifest v2,
Workload-Net Roster Manifest v2, the exact `(10200,8)` scope, canonical
cardinality, and one independently supplied clean source commit.

Only after complete Raw authentication may it open the bounded regular report
file. The join requires exact equality of source commit and stamp state,
configuration, corpus, cell plan, Raw artifact and source envelope,
repetition-zero pair and arm references, both complete semantic objects, both
telemetry checksums, every per-net closure, and the complete ordered EntityRef
roster. H=2250 Raw, the prior `(10200,4)` report authority, Wire 2, a foreign
cell, FIFO/device input, or a rechecksummed association is rejected.

The report implementation and Raw producer must be present in the same clean
commit used for acquisition. An earlier Raw artifact cannot be relabeled or
joined across commits.

The report contains no measured timing, lifecycle, utilization, CPU, RSS, or
memory observation. It cannot replace Raw outcome/timing authority and opens
no same-run, operational, Oracle, heldout, imported, matrix, or decision path.
