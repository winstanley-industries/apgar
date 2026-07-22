# ADR-030: Phase 4 Per-Net Report Artifact

**Status:** Accepted for the sixteenth Phase 4 vertical slice; publication join added by ADR-031
**Date:** July 20, 2026
**Applies to:** Canonical diagnostic serialization and representative net-roster identity

## Context

ADR-029 retains authentic per-net telemetry in process, but Raw Evidence v1
omits it to preserve a reviewed measurement and wire contract. A report must
join diagnostics to one successful raw cell without claiming the diagnostic
rerun supplied timing or resource evidence. Workload checksums and net counts
alone are insufficient: a generation-changed or same-count foreign roster
could otherwise be rechecksummed into a report.

## Decision

- Adopt `schemas/benchmark/phase4_per_net_report_artifact_v1.md` and the frozen
  `schemas/benchmark/phase4_workload_net_roster_manifest_v1.json`.
- Bind exact clean stamped source, complete canonical cell configuration, Raw
  Wire/cell-plan/cell-artifact/source-envelope checksums, and the complete
  successful repetition-zero baseline-first raw reference.
- Mark the report permanently non-decision-eligible and include no measured
  timing, utilization, lifecycle, or memory observations.
- Require baseline-then-candidate arm order and equality between diagnostic and
  referenced raw semantic checksums.
- Factor semantic-only validation out of measured finalization so measured and
  diagnostic paths reject identical enum, arithmetic, feasible-outcome,
  component-checksum, and baseline/candidate-source drift.
- Rebuild the representative case and validate telemetry against its authentic
  workload. Compare every complete EntityRef with a domain-separated roster
  checksum pinned for all 38 successful representative-manifest rows.
- Retain explicit exclusions for fixed-query 2000/2001 and compiled-work-bound
  stress 3001/3002. Their evidence kinds belong to the later query/stress slice.
- Emit deterministic compact one-line JSON. ADR-031 supplies diagnostic process
  execution and the independent Raw/report file join without changing this
  artifact or Raw v1.

## Consequences

Per-net yield, diversity, and selected-candidate quality now have a canonical,
checksum-covered representation tied to the exact raw cell and authentic
workload roster. Recomputed checksums cannot turn a same-count foreign case,
unknown enum, wrong operational identity, or impossible contender-component
shape into a valid report.

The in-process validator can authenticate raw-reference structure but cannot
prove a separate raw file exists. ADR-031's independent join validator and
external expected-commit input now provide that publication proof. This slice
does not advance query/stress evidence, statistics, legalization, or Phase 4
completion.
