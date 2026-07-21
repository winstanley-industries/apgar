# Phase 4 Per-Net Report Artifact v1

Per-Net Report Artifact v1 is the canonical diagnostic companion to one
successful Phase 4 Isolated Raw Evidence v1 cell. It carries no measured time,
utilization, or memory observation and has the literal
`decision_eligible=false`. Raw Evidence v1 and subprocess Wire v1 are unchanged.

## Source and raw association

The report requires a 40-character lowercase hexadecimal source commit, a true
source-stamped bit, and a false dirty-tree bit. Its source-envelope checksum
uses `APGAR-PHASE4-PER-NET-REPORT-SOURCE-ENVELOPE-V1` and hashes the commit,
the two source bits, and report artifact checksum. Publication still needs an
independently supplied expected commit; this in-process check cannot prove
which commit a caller intended.

The report stores the complete `Phase4CanonicalCellConfig`, Raw Wire schema,
raw cell-plan checksum, raw cell-artifact checksum, and raw source-envelope
checksum. It independently reconstructs the cell-plan and Raw source-envelope
checksums. It references exactly repetition zero in baseline-first order,
including raw pair-attempt, paired semantic/artifact, and both arm
semantic/artifact checksums. The later independent join validator MUST require
those values to equal the separate raw JSON record.

The referenced cell has exactly 20 repetitions and four preparation workers.
Both diagnostics have repetition zero, baseline-first order, and four workers,
even though execution order and worker count are absent from semantic checksum.

## Frozen workload roster

`phase4_workload_net_roster_manifest_v1.json` independently pins all 38 rows
whose `build_status` is `success` in
`phase4_representative_manifest_v1.json`. Every row records corpus, case,
descriptor, built-case, Board, workload, and net-count identities plus a full
roster checksum. The roster checksum domain is
`APGAR-PHASE4-WORKLOAD-NET-ROSTER-V1` and encodes, in order:

1. roster schema `u32`, corpus version `u32`, and corpus checksum `u64`;
2. case ID `u32`, descriptor fingerprint `u64`, case checksum `u64`, Board
   content hash `u64`, workload checksum `u64`, and net count `u64`; and
3. every canonical workload net as EntityRef ID `u64` and generation `u32`.

The manifest checksum domain
`APGAR-PHASE4-WORKLOAD-NET-ROSTER-MANIFEST-V1` encodes its schema, corpus
version/checksum, all 38 complete successful rows, then four explicit
exclusions. Cases 2000/2001 are descriptor-only fixed-query shapes whose pool
sizes are outside paired Raw v1. Cases 3001/3002 have frozen compiled-work-bound
dispositions. None may be substituted for a successful paired report; a later
query/stress slice owns their publishable artifact kinds.

Case 4000 rebuilds only from the exact source-pinned imported fixture bytes.
Other cases rebuild through the representative synthetic builder. Validation
compares the rebuilt case and roster with pinned constants and never accepts a
caller-derived replacement row.

## Arm records and validation

`arms` always contains Sequential Baseline then Reusable Candidate Allocation.
Each record contains the matching raw semantic checksum and complete diagnostic
semantics plus Per-Net Arm Telemetry v1. The diagnostic semantic checksum must
equal the raw arm semantic checksum.

Validation reuses the same full semantic validator as measured-arm
finalization, including known enums, counter closure, per-query work bounds,
feasible outcome shape, baseline/candidate source, and required or forbidden
component-checksum shapes. It then requires exact cell/config/built-case
identities and invokes the authentic-workload telemetry validator.

The report artifact checksum domain is
`APGAR-PHASE4-PER-NET-REPORT-ARTIFACT-V1`. It encodes schema, Raw Wire schema,
every cell-config field, corpus and raw association checksums, all raw-reference
fields, the false eligibility bit, roster checksum, and for each ordered arm
its enum, raw semantic checksum, diagnostic semantic checksum, and telemetry
checksum. Lower checksums cover all semantics and per-net records.

## Canonical JSON and boundary

Serialization emits one compact UTF-8 JSON object with fixed key order, decimal
integers, lowercase booleans, required JSON string escaping, and one terminal
LF. It contains configuration budget caps but no measured arm timing, process
lifecycle, utilization, or resource observations.

This slice supplies the C++ DTO, serializer, frozen roster, and rebuilding
validator only. It does not launch a diagnostic process, read a raw artifact,
perform an external expected-commit join, publish evidence, compute statistics,
or change Raw/Wire v1.
