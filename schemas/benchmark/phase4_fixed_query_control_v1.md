# Phase 4 Fixed-Query Control v1

This diagnostic publication closes the five frozen fixed-query cells without
misrepresenting them as equal whole-trial executions. Its only common
opportunity is initial candidate preparation: every descriptor has
`workload_net_count * requested_pool_size = 1024` route queries and the
canonical one-billion-work-unit per-query cap, for a total initial opportunity
of 1,024,000,000,000 work units.

## Frozen controls

The controls are ordered by case ID:

| Case | Nets | Requested pool | Initial queries | Requested within-net pairs | Evidence |
|---|---:|---:|---:|---:|---|
| 2000 | 1 | 1024 | 1024 | 523776 | descriptor-only excluded |
| 2001 | 1024 | 1 | 1024 | 0 | descriptor-only excluded |
| 2002 | 256 | 4 | 1024 | 1536 | Raw/report/operational success |
| 2003 | 128 | 8 | 1024 | 3584 | Raw/report/operational success |
| 2004 | 64 | 16 | 1024 | 7680 | Raw/report/operational success |

The pair count is exactly the sum over nets of `K * (K - 1) / 2`. Cross-net
candidate pairs are forbidden. This arithmetic describes the requested pool
shape for all five controls. For executed controls, the artifact separately
publishes the observed sum of per-net final-pool pair counts from both report
arms; it never substitutes requested opportunity for observed final pools.

Cases 2000 and 2001 are intentionally descriptor-only. They carry the frozen
descriptor fingerprint, shape arithmetic, and an explicit unavailable
execution-measurement tag. They have no case-build, timing, actual-work,
candidate, allocation, or memory fields. Numeric zero must not be used for an
unperformed measurement. Their endpoint role is to expose the one-net/many-
alternatives and many-nets/one-candidate shapes without constructing unsafe or
unrepresentative evidence.

## Executed authority join

Cases 2002-2004 each require a complete canonical Raw Evidence v1 cell, its
strictly joined Per-Net Report v1 companion, and its rebuilding Operational
Projection v1. Each authority is validated before use against an independently
supplied clean commit. The three cells must share the exact source identity,
host environment, preparation worker count, repetitions, setup/watchdog caps,
external budget, and corpus limits. The artifact binds every authority's
artifact and source-envelope checksum, the Raw cell-plan/environment/run/
controller identities, and derives all reported values from those authorities.

The executed whole-trial equal-budget opportunity remains equal between the
baseline and candidate arms within each cell. It is deliberately unequal
between shapes: cases 2002, 2003, and 2004 have 1536, 1280, and 1152 route
queries respectively because the candidate contract reserves two regeneration
queries per net. Therefore cold, prepared, outer, or whole-trial time across
these three cells is not an equal-query comparison. The control comparison
scope is only the common initial-preparation opportunity. Actual preparation
queries/work are published separately and must stay within 1024 queries,
1,024,000,000,000 work units, and the per-query cap.

## Eligibility, serialization, and bounds

`fixed_query_coverage_complete=true` means only that all five dedicated
control rows are present. `standalone_decision_eligible=false` and
`phase4_complete=false` are fixed. This artifact is diagnostic and cannot
decide the allocator hypothesis or supply missing matrix, stress, telemetry,
provenance, or statistics evidence.

Serialization is compact UTF-8 JSON in fixed key order followed by one LF.
Input and output are bounded to 4 MiB and nesting to 64 levels. Duplicate keys,
non-JSON constants, invalid UTF-8, noncanonical numeric types or bytes, and
extra, missing, or reordered fields are rejected. Validation rebuilds the
entire artifact from the nine external authorities and requires exact recursive
type, key-order, list-order, and value equality.

`artifact_checksum` hashes domain
`APGAR-PHASE4-FIXED-QUERY-CONTROL-ARTIFACT-V1` plus the canonical payload before
the two root checksums. `source_envelope_checksum` hashes domain
`APGAR-PHASE4-FIXED-QUERY-CONTROL-SOURCE-ENVELOPE-V1`, schema, source identity,
and artifact checksum. Rehashing a mutated publication cannot make it valid.
