# Phase 4 Stress Evidence v1

This diagnostic publication closes the three frozen stress-ladder cells while
keeping complete execution, deterministic capacity projection, and observed
memory measurements distinct. The declared target is 4096 distinct nets at
requested pool size 4. The largest fully built and executed tier is 1024 nets;
the 2048- and 4096-net descriptors terminate at the representative corpus's
compiled-node work bound.

## Case 3000: full Raw success

Case 3000 contains 1024 distinct nets and has frozen logical requirements of
69,177,344 compiled nodes, 1,734,049,792 estimated compiled host bytes, 7,168
active regions, and 3,074 Board entities. A publishable row requires a complete
canonical Raw Evidence v1 cell at pool 4, its strict per-net report join, and
its rebuilding Operational Projection v1. All three authorities are validated
against an independently supplied clean commit.

The stress artifact publishes all twenty repetitions' nested case-build,
prepared, cold, and outer intervals and the two `wait4` process-lifetime peak
host-byte observations. The peaks retain the exact scope
`long_lived_worker_including_warmup_and_all_repetitions`; they are not
per-repetition RSS. CPU utilization remains explicitly unavailable and GPU
utilization not applicable. The logical compiled-host-byte requirement is an
estimate from the frozen corpus manifest, not a measured peak.

## Cases 3001 and 3002: bounded builder witnesses

The work-bound authority is a source-stamped C++ probe that invokes the real
representative-case builder with its default limits:

- 4,096 maximum nets;
- 100,000,000 compiled nodes;
- 8,589,934,592 estimated compiled host bytes;
- 250,000 active regions; and
- 100,000 Board entities.

The builder materializes the descriptor and Board and compiles one authentic
representative net. Uniform per-net arithmetic then rejects the tier before a
full MultiNetWorkload, capacity model, candidate preparation, or allocator can
be reached. The artifact must describe this scope exactly; it must not label a
capacity-derived prefix as prepared, materialized, achieved, or timed.

| Case | Nets | Required nodes | Required logical bytes | Per-net nodes | Per-net bytes | Node-bound prefix | First unpreparable net |
|---|---:|---:|---:|---:|---:|---:|---|
| 3001 | 2048 | 276,879,360 | 6,941,540,352 | 135,195 | 3,389,424 | 739 | EntityRef(1739,0) |
| 3002 | 4096 | 1,107,087,360 | 27,747,614,720 | 270,285 | 6,774,320 | 369 | EntityRef(1369,0) |

Both rows stop on compiled nodes. Case 3002 also exceeds the aggregate logical
host-byte limit, but the first rejected net is node-limited; its
`limiting_work_bound` is therefore compiled nodes only, not a combined enum.
Required bytes are logical estimates and must never be represented as RSS.
Elapsed time and peak host memory are tagged unavailable because this bounded
probe provides neither a timed full build nor a `wait4` observation.

## Source, limits, and eligibility

The Raw success cell and work-bound probe must name the same clean source,
representative-corpus checksum, and exact default corpus limits. The artifact
binds Raw, report, operational, and probe artifact/source-envelope checksums,
plus the Raw environment and authority identities.

`stress_ladder_coverage_complete=true` means only that one full-success row and
both required work-bound rows are present. `maximum_full_raw_success_net_count`
is 1024 and `declared_target_fully_supported=false` is fixed.
`standalone_decision_eligible=false` and `phase4_complete=false` are also fixed.
This result documents a scalability boundary; it does not claim 2048- or
4096-net throughput, support, completion, allocation, or Phase 4 success.

## Canonical serialization and checksums

The C++ probe is compact canonical UTF-8 JSON plus one LF, bounded to 64 KiB.
Its validator enforces fixed field/list order, exact JSON types, the frozen
descriptor/manifest identities, quotient and first-net arithmetic, default
limits, source identity, and C++/Python checksum agreement. The final stress
artifact is bounded to 4 MiB and 64 nesting levels. Duplicate keys, non-JSON
constants, invalid UTF-8, noncanonical bytes or numeric types, and extra,
missing, or reordered fields are rejected. Final validation rebuilds the whole
artifact from all four external authorities and requires exact recursive type,
key-order, list-order, and value equality.

The probe domains are
`APGAR-PHASE4-STRESS-WORK-BOUND-PROBE-ARTIFACT-V1` and
`APGAR-PHASE4-STRESS-WORK-BOUND-PROBE-SOURCE-ENVELOPE-V1`. The final domains
are `APGAR-PHASE4-STRESS-EVIDENCE-ARTIFACT-V1` and
`APGAR-PHASE4-STRESS-EVIDENCE-SOURCE-ENVELOPE-V1`. Rehashing a mutated probe or
publication cannot make it valid.
