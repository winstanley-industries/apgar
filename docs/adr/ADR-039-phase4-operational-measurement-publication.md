# ADR-039: Phase 4 Operational Measurement Publication

**Status:** Accepted for the twenty-sixth Phase 4 vertical slice

**Applies to:** Complete per-cell operational telemetry and reproducibility
provenance for the frozen Phase 4 matrix

## Context

Operational Projection v1/v2 derives only fields already present in Raw
evidence. It cannot supply the completion protocol's missing process CPU,
per-arm peak host memory, complete stage timing, CPU/GPU applicability,
compatible-batch/cache applicability, or toolchain and hardware provenance.
Adding those measurements to Raw would change an already frozen decision
authority and would contaminate contender timing with publication work.

The compact candidate replay witness is useful for joining a measured profile,
but it does not by itself prove that every live session field covered by the
session checksum was present when the witness was produced.

## Decision

- Adopt the distinct Operational Measurement Publication v1 specified by
  `schemas/benchmark/phase4_operational_measurement_publication_v1.md`.
- Require Statistical Decision Protocol v4 as the publication authority. It
  substitutes this complete publication for the legacy projection on all 100
  successful cells without changing the frozen logical matrix or decision
  rules.
- Preserve Raw Evidence v1/v2, Same-Run Decision Telemetry v1, and Operational
  Projection v1/v2 byte-for-byte.
- Run four distinct exec children per successful cell: measured baseline,
  measured candidate, unmeasured baseline replay authority, and unmeasured
  candidate replay authority.
- Let the controller own exact-child `wait4` CPU/RSS, monotonic fork-to-reap
  outer time, pidfd-backed termination, process identity, limits, and
  provenance. The two authority children publish no numeric process resource
  measurements.
- Require an empty pre-dispatch child roster and temporary child-subreaper
  authority. Reject, pidfd-kill, and exactly reap every adopted descendant,
  including children that escape the leader's session/process group or close
  all capture pipes, before restoring controller state. If emptiness cannot be
  proven, retain subreaper authority and fail the controller without another
  dispatch or publication.
- Snapshot controller affinity, cgroup namespace/mount identity, unified path,
  and namespace-visible controls before and after every dispatch. Require the
  exact child to match affinity and unified path both after fork and before
  exact reap; reject observed drift. Bind the ancestry scope explicitly rather
  than claiming visibility above a cgroup namespace root.
- Open and hash one worker executable descriptor before dispatch, execute all
  four children through that pinned descriptor, bind its file identity in
  provenance, and reject in-place byte drift across the capture.
- Recompute each authority session checksum from the complete live result
  preimage before destruction. Candidate authority also emits a compact witness
  that must equal the separately measured witness field-for-field.
- Join the capture only after complete version-appropriate Raw validation and
  the corresponding legacy operational projection. Bind all source, config,
  process, worker, replay, capture, and provenance authorities.
- Publish CPU-only GPU/device/upload/readback/batch/cache fields with typed
  applicability, never numeric zero.
- Mark one complete publication as eligible input to the later matrix
  aggregator, but never as standalone decision evidence or complete matrix
  coverage.
- Support atomic, no-replacement file installation for durable publication.
  A post-link durability or cleanup failure removes the final name when it
  still names the private temporary inode, so a failed publication can be
  retried safely.

## Consequences

Every successful Raw cell can carry the missing operational and provenance
context without redefining its outcome authority. Measurements and replay
authority are isolated, and the publication explicitly distinguishes measured
values from not-applicable or unmeasured fields.

This slice still does not aggregate the 104-cell matrix, evaluate statistical
or guardrail rules, or complete Phase 4.
