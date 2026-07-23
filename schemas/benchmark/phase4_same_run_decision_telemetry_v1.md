# Phase 4 Same-Run Decision Telemetry v1

This artifact is the decision-authoritative companion for the Phase 4
exact-validation rejection guardrail. It is produced only by a telemetry-aware
isolated cell execution that returns Same-Run Raw Evidence v2 and this companion
from the same worker responses. An existing Raw cell, Per-Net Report v1,
diagnostic rerun, or post-hoc join cannot be promoted into this artifact.

The companion is required only for the 78 decision cells assigned by
`phase4_statistical_decision_protocol_v2.json`: the three exact-small cells,
the 72 held-out cells, and the three imported cells. Every such cell contains
20 paired repetitions and therefore exactly 40 successful arm observations.
Calibration, fixed-query, and stress cells do not use this authority.

## Capture boundary

Wire v2 preserves the Ready, Run, Stop, Failure, and Stopped state-machine
semantics of Wire v1. Its success response atomically carries both the ordinary
`Phase4TrialArmExecution` and the minimal telemetry leaf captured while that
same contender's authentic columns, workload, and selected world remain alive.
Wire v1 and Raw Evidence v1 are unchanged. The ordinary measured half of this
mode uses the distinct root envelope specified by
`phase4_same_run_raw_evidence_v2.md`; Raw-v1 validation intentionally rejects
it.

The telemetry-aware controller applies the same process isolation, watchdog,
resource, association, teardown, exact-`wait4`, stderr/stdout, and later-exit
rules to both outputs. A missing response, typed failure, timeout, signal,
nonzero exit, protocol error, external-budget failure, incomplete teardown, or
later process invalidation makes the companion incomplete. It never becomes a
zero rejection count or an allocator loss.

The bounded per-net partition scan is part of the measured telemetry-enabled
contender invocation. It excludes the quadratic diversity and overlap work
performed by Per-Net Report v1. Timing remains diagnostic and every decision
cell must use the same telemetry-enabled mode.

## Arm leaf

Each leaf contains, in canonical order:

- schema version `1`;
- the exact associated arm semantic checksum;
- all six fields of the copied `Phase4BoardOutcome`;
- exactly `N` rows in strict complete-`EntityRef` order; and
- the leaf checksum.

Each row contains the complete net ID and generation plus these nine unsigned
column counters:

1. requested columns;
2. executed route queries;
3. admitted candidates;
4. duplicate candidates;
5. disconnected columns;
6. unsupported columns;
7. proof-backed skipped columns;
8. exact-validation rejections; and
9. other rejections.

For each row:

```text
requested = executed + skipped
requested = admitted + duplicate + disconnected + unsupported
            + skipped + exact_validation_rejections + other_rejections
```

The complete roster must equal the independently frozen workload-net roster;
no absent, extra, duplicated, reordered, or generation-changed row is allowed.
Aggregates must close to the same execution semantics: requested columns,
actual route queries, admitted candidates, and rejected columns. The copied
outcome must equal that execution's complete outcome.

Only `CandidateRejectionCode::kExactValidation` enters
`exact_validation_rejections`. Every other build or admission rejection enters
`other_rejections`.

The leaf checksum uses byte-stable FNV-1a with domain
`APGAR-PHASE4-SAME-RUN-ARM-DECISION-TELEMETRY-V1`, followed by the schema,
associated semantic checksum, all six outcome fields, row count, and every row
field in serialized order. This structural checksum is not a cryptographic
attestation.

## Parent associations

Each arm companion binds:

- arm, repetition, order, dispatch ordinal, and process-instance identity;
- Raw arm-attempt checksum;
- Raw semantic, arm-artifact, and external-authority checksums; and
- the same-run telemetry leaf and arm-companion checksum.

Each pair binds its case, pool, repetition, root seed, order, Raw pair-attempt,
paired-semantic, and paired-artifact checksums plus baseline then candidate arm
companions. The cell artifact root binds:

- schema `1`, same-run Raw Evidence schema `2`, Raw Wire schema `2`, and
  telemetry Wire schema `2`;
- complete canonical cell configuration;
- corpus, cell-plan, environment, authority-run, and controller identities;
- Raw cell-artifact and Raw source-envelope checksums;
- exactly 20 ordered pair companions.

The Raw source-envelope checksum transitively binds the exact source commit,
stamp, and dirty state to the Raw-v2 cell. The companion source envelope binds
those same source fields to the complete companion cell-artifact checksum.

Arm, pair, cell, and source-envelope checksums use distinct
`APGAR-PHASE4-SAME-RUN-...-V1` domains and hash the fields enumerated above in
canonical serialization order. Validators must still compare complete copied
fields and associations; checksum equality alone is insufficient.

## Publication validation and decision use

The independent validator first validates the complete Same-Run Raw Evidence
v2 cell
against an independently supplied expected commit. It then requires canonical
one-line UTF-8 JSON, exact key order, no duplicate keys, maximum nesting 64,
and a maximum artifact size of 32 MiB. It reconstructs every checksum and joins
all 20 pairs and 40 arms through every association named above.

The validator independently loads the frozen statistical protocol and workload
roster. A companion for a cell outside the exact, held-out, or imported groups
is rejected. For decision eligibility it requires zero exact-validation
rejections in every row of every one of the 40 arm observations. Counts may not
be pooled across repetitions, arms, cells, pools, or families to offset a
failure. An authentic nonzero count remains valid evidence and produces an
explicit guardrail failure; it is not corrupt or incomplete evidence.

This artifact decides only the exact-rejection guardrail. Its joined Same-Run
Raw Evidence v2 cell is authoritative for board outcomes, timing, external
resources, and the paired comparison in this execution mode. Raw Evidence v1
remains the authority for legacy Wire-v1 cells and is never silently mixed with
this mode. Per-Net Report v1 remains the non-decision diagnostic authority for
candidate yield, diversity, and selected-candidate detail.
