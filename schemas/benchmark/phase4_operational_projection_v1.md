# Phase 4 Operational Projection v1

Phase 4 Operational Projection v1 is a deterministic, arm-centric projection
of one complete canonical Phase 4 Isolated Raw Evidence v1 cell. It adds no
measurement authority. The production projector first calls the Raw v1
publication validator with an independently supplied expected commit and
derives every value only from the returned document. Raw Evidence v1 and Wire
v1 are unchanged.

## Eligibility and limits

A projection exists only for a successful canonical Raw cell with exactly 20
paired repetitions, four preparation workers, ten AB orders, ten BA orders,
dispatch ordinals 1 through 40, and two distinct persistent arm processes.
`eligible_input_to_statistics=true` means only that the source Raw cell is
eligible for later matrix statistics. `standalone_decision_eligible=false`,
`coverage_complete=false`, and
`phase4_telemetry_complete=false` are fixed. One cell projection cannot close
the Section 29.2 evidence matrix, uncertainty analysis, or telemetry report.

## Raw bindings

The root preserves the clean source identity and binds the Raw wire schema and
complete Raw configuration, representative-corpus checksum, cell-plan
checksum, host-environment checksum and object, authority/controller
identities, Raw artifact checksum, and Raw source-envelope checksum.
`cell_identity` carries the
descriptor, built-case, Board IR, workload, capacity, budget, net-count, and
root-seed identities authenticated by every Raw semantic record.

Each repetition preserves its pair attempt, paired semantic, and paired
artifact checksums. Each arm preserves its attempt, semantic, record artifact,
and external-authority checksums. Thus the projection binds all 20 pair
attempts and all 40 arm attempt/semantic/record/authority layers rather than
joining only a representative repetition.

## Derived operational fields

For each arm and repetition the projection records:

- case-build, prepared, cold, and outer elapsed nanoseconds;
- `cold_residual = cold - case_build - prepared` and
  `outer_residual = outer - cold`;
- opportunity and actual route queries/work units;
- preparation and regeneration route queries/work units;
- requested, admitted, rejected, and final candidate counts;
- process identity, dispatch ordinal, and complete preparer lifecycle.

The rebuilding validator obtains all values again from the validated Raw cell,
so it checks nesting, work/accounting closure, the exact AB/BA schedule,
dispatch sequence, continuous lifecycle, process identity, and one identical
peak per arm process through the Raw validator before exact comparison. Each
process row carries the one process-lifetime peak host byte value and its
configured wall, address-space, and peak-host caps. Peak RSS is explicitly labeled
`long_lived_worker_including_warmup_and_all_repetitions`; it is never
represented as a per-repetition measurement. Ratios and cross-repetition
summaries belong to the later statistics aggregator and are not projected.

## Explicitly absent measurements

`measurement_availability` uses only tagged `{status, reason}` objects. CPU
utilization, batch fill, prepared-view cache counters, finer prepared stages,
import, compilation, cache-miss, transient-release, and per-repetition RSS are
`unavailable`. GPU utilization, device memory, and initial upload are
`not_applicable` because both contenders are CPU-only. No absent measurement
is encoded as numeric zero. Complete toolchain/hardware provenance is also
explicitly unavailable: Raw's compiler identity and host labels are partial,
not a reproducibility manifest. The execution-model annotation states that the
current prepared view is precompiled direct per-net execution with no runtime
cache measurement; it is not a cache-hit claim.

## Canonical serialization and checksums

Serialization is compact UTF-8 JSON in fixed key order followed by one LF.
Both output and input are bounded to 16 MiB; input is bounded to 64 nesting
levels. Duplicate keys, non-JSON
constants, invalid UTF-8, extra/missing/reordered fields, non-exact JSON types,
and noncanonical bytes are rejected.

`artifact_checksum` hashes domain
`APGAR-PHASE4-OPERATIONAL-PROJECTION-ARTIFACT-V1` and the canonical JSON of
every preceding field. `source_envelope_checksum` hashes domain
`APGAR-PHASE4-OPERATIONAL-PROJECTION-SOURCE-ENVELOPE-V1`, schema, exact source
commit/stamp/dirty state, and artifact checksum. Acceptance rebuilds the
complete object from the independently validated Raw cell and requires exact
recursive value, type, field-order, and list-order equality before rechecking
both checksums. Rehashing a mutated projection therefore cannot make it valid.

This schema defines neither statistics nor a Phase 4 completion decision.
