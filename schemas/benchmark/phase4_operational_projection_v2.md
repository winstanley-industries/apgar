# Phase 4 Operational Projection v2

Operational Projection v2 is a deterministic arm-centric projection of one
fully joined Same-Run Raw Evidence v2 and Same-Run Decision Telemetry v1 pair.
The production projector validates that complete two-file authority against an
independently supplied expected commit before deriving output.

The projection retains every Operational Projection v1 timing, route-work,
candidate-accounting, lifecycle, process-identity, cap, and process-lifetime
peak field. It additionally binds:

- Raw evidence schema 2 and Wire schema 2;
- the same-run telemetry artifact and source-envelope checksums;
- the observed zero-exact-rejection guardrail result; and
- every pair and arm same-run capture checksum beside the corresponding Raw
  repetition.

These bindings do not reinterpret the per-net telemetry or add measurement
authority. A false exact-rejection guardrail value is valid negative evidence
and remains visible.

`eligible_input_to_statistics=true` states only that this complete joined cell
may enter the later matrix aggregator.
`standalone_decision_eligible=false`, `coverage_complete=false`, and
`phase4_telemetry_complete=false` remain fixed. CPU utilization, compatible
batch fill, prepared-view cache counters, finer stage timing, separate
import/compile/cache-miss/release timing, per-repetition RSS, and complete
toolchain/hardware provenance remain explicitly unavailable. GPU utilization,
device memory, and initial upload remain not applicable to these CPU-only
contenders.

Canonical serialization is compact UTF-8 JSON in fixed insertion order with
one LF and a 16 MiB bound. The artifact checksum domain is
`APGAR-PHASE4-OPERATIONAL-PROJECTION-ARTIFACT-V2`; the source-envelope domain is
`APGAR-PHASE4-OPERATIONAL-PROJECTION-SOURCE-ENVELOPE-V2`. Validation rebuilds
the whole object from the already validated Raw-v2/telemetry pair and requires
exact recursive JSON types, keys, order, and values before rechecking both
checksums.

This projection defines neither matrix coverage nor a Phase 4 decision.
