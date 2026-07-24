# Phase 4 Per-Net Arm Telemetry v1

Per-Net Arm Telemetry v1 is the in-process diagnostic contract that closes the
per-net reporting fields required by the Phase 4 evidence gate. It is produced
only while an authentic Sequential Baseline v1 result or Candidate-Allocation
Session result still owns its complete columns, final pools, and selected
world. Session v3 and v4 child-authority supersessions leave this telemetry-v1
shape and checksum domain unchanged. It does not change Paired Trial v1
semantics, the subprocess wire, or Raw Evidence v1.

## Association and roster

`Phase4ArmReportTelemetryV1` has schema version `1`, the exact nonzero
`Phase4TrialArmSemantics.semantic_checksum` returned by the same execution,
exactly `N` `Phase4PerNetReportV1` records, and its own checksum. The authentic
`MultiNetWorkload` must match the arm's Board content hash and workload
checksum. Records, final pools, and selected-world outcomes must each contain
exactly the workload's `N` unique nets in strict `(EntityRef.id,
EntityRef.generation)` order. An omitted empty pool, duplicated no-candidate
selection, changed generation, or checksum-recomputed foreign net is rejected.

The producer validates the selected full immutable candidate against its final
pool before heavyweight state is destroyed. A selected record retains status,
CandidateId, payload checksum, all nine CandidateMetrics v1 values, and the
minimum intrinsic base cost in its final pool. A no-candidate record has an
empty final pool and none of those optional values. Selected intrinsic costs
sum exactly to the authenticated board outcome.

## Column outcome partition

Each net aggregates all Sequential Baseline columns, or all initial-preparation
columns followed by all targeted-regeneration epoch columns, into:

```text
requested columns
executed route queries
admitted candidates
duplicate candidates
disconnected columns
unsupported columns
skipped proof-backed columns
exact-validation rejections
other rejections
```

Only preparation's explicit disconnected/unsupported-proof suffix is skipped.
Every other successful-execution column is an executed route query. A build or
admission rejection whose CandidateRejectionCode is `kExactValidation` enters
the exact bucket; all remaining build/admission rejections enter other.
Duplicate, disconnected, unsupported, skipped, exact, and other buckets are
all rejected columns. Per net and across the arm:

```text
requested = admitted + duplicate + disconnected + unsupported
          + skipped + exact-validation + other
requested = executed + skipped
```

The aggregate requested, executed, admitted, rejected, and final-pool counts
must equal the associated arm semantics exactly. Widened arithmetic rejects
overflow.

## Pool diversity

Each record publishes final pool size, unique geometry-signature count, unique
resource-signature count, candidate-pair count, and mean/minimum resource and
geometric overlap in integer parts per million. Pair count is exactly
`pool_size * (pool_size - 1) / 2`. Empty and singleton pools have zero pairs
and four zero overlap fields.

V1 evaluates every unordered pair from the exact integer numerator and
denominator underlying the CandidateStore resource-Jaccard and geometric
projected-overlap definitions. It computes `(numerator * 1,000,000 +
denominator / 2) / denominator` in checked unsigned 128-bit arithmetic, giving
nearest-integer ppm with exact half values upward. CandidateStore's established
invalid/nonmatching-context result remains zero; arithmetic failure is distinct
and fails durable telemetry closed. The mean is computed from per-pair integer
ppm values and rounded to nearest integer with half upward; the minimum is the
minimum per-pair ppm.

## Stable checksum

The checksum uses Board IR v1 `StableHashBuilder` with domain
`APGAR-PHASE4-ARM-REPORT-TELEMETRY-V1`. After the domain encode telemetry
schema `u32`, associated semantic checksum `u64`, and record count `u64`.
For every sorted record encode, in declaration order:

- record schema `u32`, net ID `u64`, and generation `u32`;
- the nine column counters as `u64`;
- pool size, two unique-signature counts, pair count, and four overlap ppm
  values as eight `u64`;
- selected status `u8`;
- CandidateId presence `bool`, then high and low `u64` when present;
- payload-checksum presence `bool`, then its `u64` when present;
- metrics presence `bool`, then all nine CandidateMetrics `u64` values in
  declaration order when present; and
- pool-best-cost presence `bool`, then its `u64` when present.

The checksum field is excluded. The checksum detects accidental drift and is
not a substitute for the independent workload/semantic association or the
producer's full-candidate membership validation.

## Execution seam

`ExecutePhase4TrialArmDiagnosticV1` shares the contender execution path and
returns only `Phase4TrialArmSemantics` plus this telemetry. Its result type does
not contain, convert to, or expose a `Phase4TrialArmExecution`; diagnostic work
therefore cannot be passed to `FinalizePhase4TrialArmV1` or represented as raw
decision timing. The existing `ExecutePhase4TrialArmV1` takes the non-capturing
path: its type, semantic checksum, timing scope, raw wire representation, and
isolated controller behavior remain unchanged. Canonical diagnostic
serialization and representative-case rebuilding validation are defined by
`phase4_per_net_report_artifact_v1.md`; diagnostic-process execution and the
independent Raw/report file join remain separate.
